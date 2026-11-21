#include "test_auth.hpp"
#include "test_util.hpp"

#include "network/Batch.hpp"
#include "network/PacketCipher.hpp"
#include "server/Server.hpp"

#include "protocol/Packets.hpp"

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace fs = std::filesystem;

namespace {
    std::string Decode64(std::string in) {
        for (char& c : in)
            c = c == '-' ? '+' : c == '_' ? '/' : c;
        while (in.size() % 4)
            in += '=';
        std::string out(in.size() / 4 * 3, '\0');
        const int n =
            EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                            reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
        size_t len = static_cast<size_t>(std::max(n, 0));
        for (size_t i = in.size(); i > 0 && in[i - 1] == '=' && len > 0; i--)
            len--;
        out.resize(len);
        return out;
    }

    struct Packet {
        uint32_t id;
        std::vector<uint8_t> body;
    };

    class Client {
    public:
        Client(Server& server, const std::string& name)
            : server(server), endpoint(server.Loopback().Connect()), account(name, "") {}

        template <class P> void Send(const P& packet) {
            std::vector<uint8_t> body;
            net::EncodeChecked(packet, body);
            std::vector<uint8_t> content;
            BatchCodec::AppendPacket(content, P::kId, body);
            std::vector<uint8_t> wire = codec.Seal(content);
            if (cipher.Active())
                wire = cipher.Encrypt(wire);
            endpoint->Send(wire.data(), wire.size());
        }

        void SendRaw(const std::vector<uint8_t>& wire) { endpoint->Send(wire.data(), wire.size()); }

        void Settle(int ticks = 4) {
            for (int i = 0; i < ticks; i++) {
                server.Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            std::vector<uint8_t> wire;
            while (endpoint->Receive(wire)) {
                std::vector<uint8_t> plain;
                if (cipher.Active()) {
                    if (!cipher.Decrypt(wire, plain)) {
                        badChecksum = true;
                        continue;
                    }
                    wire = plain;
                }
                std::vector<uint8_t> content;
                if (!codec.Open(wire, content)) {
                    corrupt = true;
                    continue;
                }
                BatchCodec::Split(content, [&](uint32_t id, std::span<const uint8_t> body) {
                    inbox.push_back({id, std::vector<uint8_t>(body.begin(), body.end())});
                    if (id == proto::NetworkSettingsPacket::kId)
                        codec.EnableCompression(1);
                    if (id == proto::ServerToClientHandshakePacket::kId)
                        pendingHandshake = true;
                });
                if (pendingHandshake) {
                    pendingHandshake = false;
                    for (const P& p : Take<proto::ServerToClientHandshakePacket>())
                        handshakes.push_back(p.token);
                    if (!handshakes.empty())
                        StartCipher(handshakes.back());
                }
            }
        }

        template <class T> std::vector<T> Take() {
            std::vector<T> out;
            for (auto it = inbox.begin(); it != inbox.end();) {
                if (it->id == T::kId) {
                    T p;
                    net::ReadStream s(it->body.data(), it->body.size());
                    if (Read(s, p) && s.ok)
                        out.push_back(std::move(p));
                    it = inbox.erase(it);
                } else {
                    ++it;
                }
            }
            return out;
        }

        void StartCipher(const std::string& token) {
            const size_t a = token.find('.');
            const size_t b = token.find('.', a + 1);
            json::Value header, payload;
            CHECK(json::Parse(Decode64(token.substr(0, a)), header));
            CHECK(json::Parse(Decode64(token.substr(a + 1, b - a - 1)), payload));
            const std::string der = Decode64(header["x5u"].String());
            const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
            EVP_PKEY* peer = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
            CHECK(peer != nullptr);
            CHECK(testauth::VerifyJwt(token, peer));
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(account.player.pkey, nullptr);
            size_t len = 0;
            EVP_PKEY_derive_init(ctx);
            EVP_PKEY_derive_set_peer(ctx, peer);
            EVP_PKEY_derive(ctx, nullptr, &len);
            std::string secret(len, '\0');
            EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(secret.data()), &len);
            EVP_PKEY_CTX_free(ctx);
            EVP_PKEY_free(peer);
            std::string material = Decode64(payload["salt"].String()) + secret;
            uint8_t key[32];
            unsigned int keyLen = 0;
            EVP_Digest(material.data(), material.size(), key, &keyLen, EVP_sha256(), nullptr);
            CHECK(cipher.InitWithKey(key));
            cipher.Activate();
        }

        void Login() {
            proto::RequestNetworkSettingsPacket req;
            req.clientProtocol = 2225;
            Send(req);
            Settle();
            proto::LoginPacket login;
            login.protocolVersion = 2225;
            login.tokens.identity = account.Identity(account.Chain(false), 2);
            login.tokens.client = account.ClientToken();
            Send(login);
            Settle();
        }

        using P = proto::ServerToClientHandshakePacket;
        Server& server;
        std::shared_ptr<LoopbackListener::Endpoint> endpoint;
        testauth::Account account;
        BatchCodec codec;
        PacketCipher cipher;
        std::vector<Packet> inbox;
        std::vector<std::string> handshakes;
        bool pendingHandshake = false;
        bool corrupt = false;
        bool badChecksum = false;
    };

    void WritePack(const fs::path& dir) {
        fs::create_directories(dir / "textures");
        std::ofstream(dir / "manifest.json")
            << R"({"format_version":2,"header":{"name":"Test Pack","uuid":"0f4cbe4c-1f6a-4c24-8f0f-2b1f8a3a6c11","version":[1,2,3],"min_engine_version":[1,21,0]},"modules":[{"type":"resources","uuid":"5b2f4d48-0c1a-4f15-9c51-6b6bb3c3e1a2","version":[1,2,3]}]})";
        std::string big(300 * 1024, 'x');
        for (size_t i = 0; i < big.size(); i++)
            big[i] = static_cast<char>((i * 2654435761u) >> 13);
        std::ofstream(dir / "textures" / "noise.bin", std::ios::binary) << big;
    }
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "perlite_login_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "resource_packs");
    WritePack(dir / "resource_packs" / "test_pack");

    ServerConfig config;
    config.dataDir = dir.string();
    config.generator = "flat";
    config.terrainBackend = "cpu";
    config.transport = "raknet";
    config.port = 19361;
    config.bindAddress = "127.0.0.1";
    config.networkEncryption = true;
    config.encryptLocal = true;
    config.worldStorage = "none";
    Server server(config);
    std::string error;
    if (!server.Start(error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }

    Client alice(server, "Alice");
    alice.Login();
    CHECK(alice.handshakes.size() == 1);
    CHECK(alice.cipher.Active());
    CHECK(alice.Take<proto::PlayStatusPacket>().empty());
    alice.Send(proto::ClientToServerHandshakePacket{});
    alice.Settle();
    CHECK(!alice.badChecksum && !alice.corrupt);
    const auto status = alice.Take<proto::PlayStatusPacket>();
    CHECK(status.size() == 1 && status[0].status == proto::PlayStatusPacketStatus::LoginSuccess);
    const auto info = alice.Take<proto::ResourcePacksInfoPacket>();
    CHECK(info.size() == 1 && info[0].texturePacks.size() == 1);
    if (info.size() == 1 && info[0].texturePacks.size() == 1)
        CHECK(info[0].texturePacks[0].version == "1.2.3" && info[0].texturePacks[0].size > 0);

    proto::ResourcePackClientResponsePacket want;
    want.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::SendPacks;
    want.resourcepackids = std::vector<std::string>{"0f4cbe4c-1f6a-4c24-8f0f-2b1f8a3a6c11_1.2.3"};
    alice.Send(want);
    alice.Settle();
    const auto dataInfo = alice.Take<proto::ResourcePackDataInfoPacket>();
    CHECK(dataInfo.size() == 1);
    std::vector<uint8_t> assembled;
    if (dataInfo.size() == 1) {
        CHECK(dataInfo[0].chunkCount >= 1 && dataInfo[0].hash.size() == 32);
        for (uint32_t i = 0; i < dataInfo[0].chunkCount; i++) {
            proto::ResourcePackChunkRequestPacket req;
            req.packId = dataInfo[0].packId;
            req.chunkIndex = i;
            alice.Send(req);
            alice.Settle(1);
            for (const auto& chunk : alice.Take<proto::ResourcePackChunkDataPacket>()) {
                CHECK(chunk.progress == assembled.size());
                assembled.insert(assembled.end(), chunk.payload.begin(), chunk.payload.end());
            }
        }
        CHECK(assembled.size() == dataInfo[0].size);
        std::vector<uint8_t> digest(32);
        unsigned int len = 0;
        EVP_Digest(assembled.data(), assembled.size(), digest.data(), &len, EVP_sha256(), nullptr);
        CHECK(digest == dataInfo[0].hash);
    }

    proto::ResourcePackClientResponsePacket have;
    have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
    alice.Send(have);
    alice.Settle();
    const auto stack = alice.Take<proto::ResourcePackStackPacket>();
    CHECK(stack.size() == 1 && stack[0].resourcePacks.size() == 1);

    proto::ResourcePackClientResponsePacket done;
    done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
    alice.Send(done);
    alice.Settle();
    CHECK(alice.Take<proto::StartGamePacket>().size() == 1);
    const auto biomes = alice.Take<proto::BiomeDefinitionListPacket>();
    CHECK(biomes.size() == 1 && biomes[0].biomeDefinitions.size() > 50 &&
          biomes[0].stringList.size() == biomes[0].biomeDefinitions.size());
    const auto entities = alice.Take<proto::AvailableEntityIdentifiersPacket>();
    CHECK(entities.size() == 1 && entities[0].nbt.bytes.size() > 1000);
    if (entities.size() == 1) {
        const std::string raw(entities[0].nbt.bytes.begin(), entities[0].nbt.bytes.end());
        CHECK(raw.contains("minecraft:zombie") && raw.contains("minecraft:player") && raw.contains("idlist"));
    }
    CHECK(alice.Take<proto::ItemRegistryPacket>().size() == 1);
    CHECK(!alice.badChecksum && !alice.corrupt);

    Client mallory(server, "Mallory");
    mallory.Login();
    CHECK(mallory.cipher.Active());
    std::vector<uint8_t> forged = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    mallory.SendRaw(forged);
    mallory.Settle();
    CHECK(server.InGamePlayers().empty());

    server.Stop();
    fs::remove_all(dir, ec);
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_login_test ok");
    return 0;
}
