#include "test_util.hpp"
#include "test_auth.hpp"

#include "common/Json.hpp"
#include "server/Server.hpp"
#include "network/Batch.hpp"

#include "protocol/Packets.hpp"

#include "support/MiniHttp.hpp"
#include <rtc/rtc.hpp>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace net = perlite::net;

namespace {
    std::string B64Url(const std::string& s) {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string out;
        for (size_t i = 0; i < s.size(); i += 3) {
            uint32_t v = static_cast<unsigned char>(s[i]) << 16;
            if (i + 1 < s.size())
                v |= static_cast<unsigned char>(s[i + 1]) << 8;
            if (i + 2 < s.size())
                v |= static_cast<unsigned char>(s[i + 2]);
            out += t[(v >> 18) & 63];
            out += t[(v >> 12) & 63];
            if (i + 1 < s.size())
                out += t[(v >> 6) & 63];
            if (i + 2 < s.size())
                out += t[v & 63];
        }
        return out;
    }
    std::string Jwt(const std::string& payload) {
        return B64Url("{}") + "." + B64Url(payload) + ".sig";
    }

    template <class P> std::vector<uint8_t> Wire(BatchCodec& codec, const P& packet) {
        std::vector<uint8_t> body, content;
        net::EncodeChecked(packet, body);
        BatchCodec::AppendPacket(content, P::kId, body);
        return codec.Seal(content);
    }
}

namespace {
    std::string B64Decode(std::string in) {
        for (char& c : in) {
            if (c == '-')
                c = '+';
            else if (c == '_')
                c = '/';
        }
        while (in.size() % 4)
            in += '=';
        std::string out(in.size() / 4 * 3 + 3, '\0');
        const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                      reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
        if (n < 0)
            return {};
        size_t len = static_cast<size_t>(n);
        for (size_t i = in.size(); i > 0 && in[i - 1] == '='; i--)
            len--;
        out.resize(len);
        return out;
    }

    bool VerifyEs384(const std::string& x, const std::string& y, const std::string& input, const std::string& raw) {
        if (x.size() != 48 || y.size() != 48 || raw.size() != 96)
            return false;
        std::string point = "\x04" + x + y;
        OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
        OSSL_PARAM_BLD_push_utf8_string(bld, "group", "P-384", 0);
        OSSL_PARAM_BLD_push_octet_string(bld, "pub", point.data(), point.size());
        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        EVP_PKEY* key = nullptr;
        EVP_PKEY_fromdata_init(pctx);
        EVP_PKEY_fromdata(pctx, &key, EVP_PKEY_PUBLIC_KEY, params);
        ECDSA_SIG* sig = ECDSA_SIG_new();
        ECDSA_SIG_set0(sig, BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 48, nullptr),
                       BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 48, 48, nullptr));
        unsigned char* der = nullptr;
        const int derLen = i2d_ECDSA_SIG(sig, &der);
        EVP_MD_CTX* md = EVP_MD_CTX_new();
        const bool ok = key && EVP_DigestVerifyInit(md, nullptr, EVP_sha384(), nullptr, key) == 1 &&
                        EVP_DigestVerify(md, der, static_cast<size_t>(derLen),
                                         reinterpret_cast<const unsigned char*>(input.data()), input.size()) == 1;
        EVP_MD_CTX_free(md);
        OPENSSL_free(der);
        ECDSA_SIG_free(sig);
        EVP_PKEY_free(key);
        EVP_PKEY_CTX_free(pctx);
        OSSL_PARAM_free(params);
        OSSL_PARAM_BLD_free(bld);
        return ok;
    }

    bool AnswerIdentityValid(const std::string& sdp) {
        std::string identity, fingerprint;
        size_t pos = 0;
        while (pos < sdp.size()) {
            size_t end = sdp.find("\r\n", pos);
            if (end == std::string::npos)
                end = sdp.size();
            const std::string line = sdp.substr(pos, end - pos);
            if (line.starts_with("a=identity:"))
                identity = line.substr(11);
            if (line.starts_with("a=fingerprint:"))
                fingerprint = line.substr(14);
            pos = end + 2;
        }
        if (identity.empty() || fingerprint.empty())
            return false;
        json::Value envelope, assertion, claims;
        if (!json::Parse(B64Decode(identity), envelope) || envelope["idp"]["domain"].String() != "self" ||
            !json::Parse(envelope["assertion"].String(), assertion))
            return false;
        const std::string token = assertion["token"].String();
        const size_t d1 = token.find('.'), d2 = token.rfind('.');
        if (d1 == std::string::npos || d1 == d2 || !json::Parse(B64Decode(token.substr(d1 + 1, d2 - d1 - 1)), claims))
            return false;
        const std::string x = B64Decode(claims["cpk"]["x"].String()), y = B64Decode(claims["cpk"]["y"].String());
        if (!VerifyEs384(x, y, token.substr(0, d2), B64Decode(token.substr(d2 + 1))))
            return false;
        const std::string detached = assertion["fingerprints"].String();
        const size_t s1 = detached.find(".."), space = fingerprint.find(' ');
        if (s1 == std::string::npos || space == std::string::npos)
            return false;
        const std::string payload = "{\"fingerprint\":[{\"algorithm\":\"" + fingerprint.substr(0, space) +
                                    "\",\"digest\":\"" + fingerprint.substr(space + 1) + "\"}]}";
        std::string b64;
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (size_t i = 0; i < payload.size(); i += 3) {
            uint32_t v = static_cast<unsigned char>(payload[i]) << 16;
            if (i + 1 < payload.size())
                v |= static_cast<unsigned char>(payload[i + 1]) << 8;
            if (i + 2 < payload.size())
                v |= static_cast<unsigned char>(payload[i + 2]);
            b64 += t[(v >> 18) & 63];
            b64 += t[(v >> 12) & 63];
            if (i + 1 < payload.size())
                b64 += t[(v >> 6) & 63];
            if (i + 2 < payload.size())
                b64 += t[v & 63];
        }
        return VerifyEs384(x, y, detached.substr(0, s1) + "." + b64, B64Decode(detached.substr(s1 + 2)));
    }
}

namespace {
    struct Outcome {
        bool connected = false;
        bool loginSuccess = false;
        bool disconnected = false;
        int disconnectReason = -1;
        bool gotChunk = false;
    };

    Outcome JoinOver(uint16_t port, const std::string& identity, const std::string& clientToken,
                     const testauth::Key* assertWith, bool fullHandshake) {
        Outcome out;
        auto probe = minihttp::Request(port, "GET", "/v1/join?version=1.26.60&protocol=2225&platform=1&id=7");
        CHECK(probe && probe->status == 200 && probe->body.empty());
        auto info = minihttp::Request(port, "GET", "/v1/join?info");
        CHECK(info && info->status == 200);
        if (info) {
            json::Value v;
            CHECK(json::Parse(info->body, v));
            CHECK(v["protocol"].Int() == 2225 && v["version"].String() == "1.26.60");
        }

        rtc::Configuration rc;
        rc.bindAddress = "127.0.0.1";
        auto pc = std::make_shared<rtc::PeerConnection>(rc);
        std::atomic<bool> gathered{false}, open{false};
        std::mutex mutex;
        std::vector<std::vector<uint8_t>> messages;
        BatchCodec codec;
        pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState st) {
            if (st == rtc::PeerConnection::GatheringState::Complete)
                gathered = true;
        });
        auto reliable = pc->createDataChannel("ReliableDataChannel");
        rtc::DataChannelInit unrel;
        unrel.reliability.unordered = true;
        unrel.reliability.maxRetransmits = 0u;
        auto unreliable = pc->createDataChannel("UnreliableDataChannel", unrel);
        reliable->onOpen([&] { open = true; });
        reliable->onMessage([&](rtc::message_variant m) {
            if (auto* b = std::get_if<rtc::binary>(&m); b && b->size() > 1) {
                std::lock_guard lock(mutex);
                messages.emplace_back(reinterpret_cast<const uint8_t*>(b->data()) + 1,
                                      reinterpret_cast<const uint8_t*>(b->data()) + b->size());
            }
        });
        pc->setLocalDescription();
        for (int i = 0; i < 100 && !gathered; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(gathered);
        std::string offer = std::string(*pc->localDescription());
        if (assertWith) {
            offer = testauth::AssertOffer(offer, *assertWith);
        } else {

            const size_t m = offer.find("m=");
            offer.insert(offer.rfind('\n', m) + 1, "a=identity:e30=\r\n");
        }

        auto answer = minihttp::Request(port, "POST", "/v1/join/12345", offer, "application/sdp");
        CHECK(answer && answer->status == 200 && answer->body.starts_with("v="));
        CHECK(answer && answer->contentType == "application/sdp");
        CHECK(answer && AnswerIdentityValid(answer->body));
        if (!answer || answer->status != 200) {
            if (answer)
                std::fprintf(stderr, "answer status %d: %s\n", answer->status, answer->body.c_str());
            return out;
        }
        pc->setRemoteDescription(rtc::Description(answer->body, rtc::Description::Type::Answer));
        for (int i = 0; i < 200 && !open; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(open);
        out.connected = open;
        if (!open)
            return out;

        const auto send = [&](const std::vector<uint8_t>& wire) {
            for (const auto& p : SplitFragments(wire.data(), wire.size()))
                reliable->send(rtc::binary(reinterpret_cast<const std::byte*>(p.data()),
                                           reinterpret_cast<const std::byte*>(p.data()) + p.size()));
        };

        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> packets;
        const auto waitFor = [&](uint32_t id, std::vector<uint8_t>* bodyOut = nullptr, int tries = 100) {
            for (int i = 0; i < tries; i++) {
                {
                    std::lock_guard lock(mutex);
                    for (const auto& wire : messages) {
                        std::vector<uint8_t> content;
                        if (!codec.Open(wire, content))
                            continue;
                        BatchCodec::Split(content, [&](uint32_t pid, std::span<const uint8_t> body) {
                            packets.emplace_back(pid, std::vector<uint8_t>(body.begin(), body.end()));
                            if (pid == proto::NetworkSettingsPacket::kId)
                                codec.EnableCompression(256);
                        });
                    }
                    messages.clear();
                }
                for (auto it = packets.begin(); it != packets.end(); ++it) {
                    if (it->first == id) {
                        if (bodyOut)
                            *bodyOut = it->second;
                        packets.erase(it);
                        return true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return false;
        };

        proto::RequestNetworkSettingsPacket req;
        req.clientProtocol = 2225;
        send(Wire(codec, req));
        CHECK(waitFor(proto::NetworkSettingsPacket::kId));

        proto::LoginPacket login;
        login.protocolVersion = 2225;
        login.tokens.identity = identity;
        login.tokens.client = clientToken;
        send(Wire(codec, login));
        std::vector<uint8_t> body;

        for (int i = 0; i < 100 && !out.loginSuccess && !out.disconnected; i++) {
            if (waitFor(proto::PlayStatusPacket::kId, &body, 1)) {
                proto::PlayStatusPacket st;
                net::ReadStream rs(body.data(), body.size());
                if (Read(rs, st) && st.status == proto::PlayStatusPacketStatus::LoginSuccess)
                    out.loginSuccess = true;
            }
            if (waitFor(proto::DisconnectPacket::kId, &body, 1)) {
                proto::DisconnectPacket d;
                net::ReadStream rs(body.data(), body.size());
                if (Read(rs, d)) {
                    out.disconnected = true;
                    out.disconnectReason = static_cast<int>(d.reason);
                }
            }
        }
        if (out.loginSuccess && fullHandshake) {
            CHECK(waitFor(proto::ResourcePacksInfoPacket::kId));
            proto::ResourcePackClientResponsePacket have;
            have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
            send(Wire(codec, have));
            CHECK(waitFor(proto::ResourcePackStackPacket::kId));
            proto::ResourcePackClientResponsePacket done;
            done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
            send(Wire(codec, done));
            CHECK(waitFor(proto::StartGamePacket::kId));
            proto::RequestChunkRadiusPacket radius;
            radius.chunkRadius = 2;
            radius.maxRadius = 2;
            send(Wire(codec, radius));
            out.gotChunk = waitFor(proto::LevelChunkPacket::kId);
            CHECK(out.gotChunk);
        }
        pc->close();
        return out;
    }
}

int main() {
    constexpr uint16_t kPort = 19333;
    ServerConfig config;
    config.port = kPort;
    config.bindAddress = "127.0.0.1";
    config.generator = "flat";
    config.terrainBackend = "cpu";
    config.iceServers = {"none"};
    config.iceBindAddress = "127.0.0.1";
    config.iceUdpMux = std::getenv("PS_TEST_MUX") != nullptr;
    config.networkEncryption = false;
    config.viewDistance = 4;
    Server open(config);
    std::string error;
    if (!open.Start(error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    std::thread ticker([&] { open.Run(); });

    {
        const auto alert = minihttp::Exchange(kPort, std::string("\x16\x03\x01\x00\x05\x01\x00\x00\x01\x00", 10), 5);
        CHECK(alert && *alert == std::string("\x15\x03\x03\x00\x02\x02\x28", 7));
        const auto missing = minihttp::Request(kPort, "GET", "/nope");
        CHECK(missing && missing->status == 404);
    }

    {
        testauth::Account guest("Webrtc", "");
        const Outcome o =
            JoinOver(kPort, guest.Identity(guest.Chain(false), 2), guest.ClientToken(), nullptr, true);
        CHECK(o.connected && o.loginSuccess && o.gotChunk);

        const Outcome forged =
            JoinOver(kPort, guest.Identity({"a.b.c"}, 2), guest.ClientToken(), nullptr, false);
        CHECK(forged.connected && !forged.loginSuccess);
    }
    open.RequestStop();
    ticker.join();
    open.Stop();

    testauth::Account xbox("XboxPlayer", "2535412345678");
    ServerConfig strict = config;
    strict.port = kPort + 1;
    strict.xboxAuth = true;
    strict.authRootKeys = {xbox.root.spki};
    Server locked(strict);
    CHECK(locked.Start(error));
    std::thread ticker2([&] { locked.Run(); });
    {
        const Outcome good =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), &xbox.player, true);
        CHECK(good.loginSuccess && good.gotChunk);
        const Outcome offline = JoinOver(strict.port, xbox.Identity(xbox.Chain(false), 2), xbox.ClientToken(),
                                         &xbox.player, false);
        CHECK(!offline.loginSuccess && offline.disconnected && offline.disconnectReason == 46);

        testauth::Key replayer;
        const Outcome replay =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), &replayer, false);
        CHECK(!replay.loginSuccess && replay.disconnected && replay.disconnectReason == 46);
        const Outcome noAssertion =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), nullptr, false);
        CHECK(!noAssertion.loginSuccess && noAssertion.disconnected && noAssertion.disconnectReason == 46);
    }
    locked.RequestStop();
    ticker2.join();
    locked.Stop();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_nethernet_test ok");
    return 0;
}
