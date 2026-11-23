// Joins a server over LAN (NetherNet) as a self-signed (offline) player and records every packet it
// receives, for comparing two servers' behaviour packet by packet.
//
//   lan_capture <unused port> <out dir> <seconds> [script]
//
// The optional script has lines "<seconds after spawn> <console command>"; each command is written,
// at its time, to the file named by CAPTURE_CONSOLE (a FIFO feeding the server's console).
// <out dir>/summary.txt lists the entity packets decoded (type, metadata, motion, equipment).
#include "test_util.hpp"
#include "test_auth.hpp"
#include "support/MiniHttp.hpp"
#include "network/Batch.hpp"
#include "network/Connection.hpp"
#include "protocol/Packets.hpp"
#include "common/Json.hpp"
#include <rtc/rtc.hpp>
#include "network/nethernet/LanDiscovery.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <map>
#include <type_traits>
#include <variant>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace net = perlite::net;

static std::string B64(const std::string& s) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        uint32_t v = (uint8_t)s[i] << 16 | (uint8_t)s[i + 1] << 8 | (uint8_t)s[i + 2];
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += t[v & 63];
    }
    if (s.size() - i == 1) { uint32_t v = (uint8_t)s[i] << 16; o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += "=="; }
    else if (s.size() - i == 2) { uint32_t v = (uint8_t)s[i] << 16 | (uint8_t)s[i + 1] << 8; o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += '='; }
    return o;
}

template <class P> std::vector<uint8_t> Wire(BatchCodec& codec, const P& packet) {
    std::vector<uint8_t> body, content;
    net::EncodeChecked(packet, body);
    BatchCodec::AppendPacket(content, P::kId, body);
    return codec.Seal(content);
}

int main(int argc, char** argv) {
    const uint16_t port = argc > 1 ? (uint16_t)std::atoi(argv[1]) : 19150;
    const std::string outDir = argc > 2 ? argv[2] : "/tmp/cap";
    const int seconds = argc > 3 ? std::atoi(argv[3]) : 20;
    std::filesystem::create_directories(outDir);
    testauth::Account me("Capture", "");
    rtc::InitLogger(std::getenv("CAP_RTC") ? rtc::LogLevel::Debug : rtc::LogLevel::Error);
    rtc::Configuration rc;
    auto pc = std::make_shared<rtc::PeerConnection>(rc);
    std::atomic<bool> gathered{false}, open{false};
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> messages;
    FragmentAssembler assembler;
    pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState st) {
        if (st == rtc::PeerConnection::GatheringState::Complete) gathered = true;
    });
    auto reliable = pc->createDataChannel("ReliableDataChannel");
    rtc::DataChannelInit unrel;
    unrel.reliability.unordered = true;
    unrel.reliability.maxRetransmits = 0u;
    auto unreliable = pc->createDataChannel("UnreliableDataChannel", unrel);
    reliable->onOpen([&] { open = true; });
    reliable->onMessage([&](rtc::message_variant m) {
        if (auto* b = std::get_if<rtc::binary>(&m)) {
            std::vector<uint8_t> whole;
            std::lock_guard lock(mutex);
            if (assembler.Push(reinterpret_cast<const uint8_t*>(b->data()), b->size(), whole))
                messages.push_back(std::move(whole));
        }
    });
    pc->setLocalDescription();
    for (int i = 0; i < 100 && !gathered; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // LAN join: discovery and signalling over UDP 7551, no identity on the offer.
    namespace lan = perlite::nethernet::lan;
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    {
        int yes = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);
        ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(lan::kPort);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
            // Port 7551 is the server's alone here: answers come back to wherever we send from.
            a.sin_port = 0;
            ::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        }
    }
    const uint64_t myId = 0x1122334455667788ull ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t server = 0;
    const auto sendLan = [&](uint16_t type, const std::string& body) {
        const std::string d = lan::Encode(type, myId, body);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(lan::kPort);
        to.sin_addr.s_addr = htonl(type == lan::kRequest ? INADDR_BROADCAST : INADDR_LOOPBACK);
        ::sendto(sock, d.data(), d.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        if (type == lan::kRequest) {
            to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::sendto(sock, d.data(), d.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
    };
    const auto recvLan = [&](uint16_t& type, std::string& body, int ms) {
        pollfd p{sock, POLLIN, 0};
        std::string buf(65536, '\0');
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (::poll(&p, 1, 50) <= 0) continue;
            const ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
            uint64_t sender = 0;
            if (n > 0 && lan::Decode(buf.substr(0, n), type, sender, body) && sender != myId) { server = sender; return true; }
        }
        return false;
    };
    {
        sendLan(lan::kRequest, {});
        uint16_t type = 0;
        std::string body;
        bool found = false;
        while (recvLan(type, body, 3000))
            if (type == lan::kResponse) {
                std::string data;
                lan::ParseResponseBody(body, data);
                std::ofstream(outDir + "/lan_response.bin", std::ios::binary) << data;
                found = true;
                break;
            }
        if (!found) { std::printf("no LAN response\n"); return 1; }
        std::printf("LAN server id %llu\n", (unsigned long long)server);
    }
    const std::string full = std::string(*pc->localDescription());
    std::string bare;
    std::vector<std::string> cands;
    std::string ufrag;
    {
        std::istringstream in(full);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("a=ice-ufrag:")) ufrag = line.substr(12);
            if (line.starts_with("a=candidate:")) cands.push_back(line.substr(2));
            if (line != "a=end-of-candidates") bare += line + "\r\n";
        }
    }
    sendLan(lan::kMessage, lan::MessageBody(server, "CONNECTREQUEST 4242 " + bare));
    for (auto& c : cands)
        sendLan(lan::kMessage, lan::MessageBody(server, "CANDIDATEADD 4242 " + c + " generation 0 ufrag " + ufrag + " network-id 1 network-cost 10"));
    {
        bool answered = false;
        uint16_t type = 0;
        std::string body;
        for (int i = 0; i < 200 && !open; i++) {
            if (!recvLan(type, body, 100) || type != lan::kMessage) continue;
            uint64_t to = 0;
            std::string text;
            if (!lan::ParseMessageBody(body, to, text) || to != myId) continue;
            if (std::getenv("CAP_DEBUG")) std::printf("signal: %.60s\n", text.c_str());
            if (text.starts_with("CONNECTRESPONSE 4242 ")) {
                const std::string ans = text.substr(21);
                std::ofstream(outDir + "/answer.sdp") << ans;
                std::string clean;
                std::istringstream in(ans);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.starts_with("a=identity")) clean += line + "\r\n";
                }
                pc->setRemoteDescription(rtc::Description(clean, rtc::Description::Type::Answer));
                answered = true;
            } else if (text.starts_with("CANDIDATEADD 4242 ") && answered) {
                try { pc->addRemoteCandidate(rtc::Candidate(text.substr(18), "0")); } catch (...) {}
            } else if (text.starts_with("CONNECTERROR")) {
                std::printf("lan error: %s\n", text.c_str());
                return 1;
            }
        }
    }
    for (int i = 0; i < 200 && !open; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!open) { std::printf("data channel did not open\n"); return 1; }
    BatchCodec codec;
    const auto send = [&](const std::vector<uint8_t>& wire) {
        for (const auto& p : SplitFragments(wire.data(), wire.size()))
            reliable->send(rtc::binary(reinterpret_cast<const std::byte*>(p.data()), reinterpret_cast<const std::byte*>(p.data()) + p.size()));
    };
    proto::RequestNetworkSettingsPacket req;
    req.clientProtocol = 2225;
    send(Wire(codec, req));
    std::string skin(64 * 64 * 4, '\xff');
    const std::string patch = "{\"geometry\":{\"default\":\"geometry.humanoid.custom\"}}";
    const std::string clientData = std::string("{") +
        "\"ThirdPartyName\":\"Capture\",\"SkinId\":\"c18e65aa-7b21-4637-9b63-8ad63622ef01.Steve\",\"SkinData\":\"" + B64(skin) +
        "\",\"SkinImageWidth\":64,\"SkinImageHeight\":64,\"SkinResourcePatch\":\"" + B64(patch) +
        "\",\"SkinGeometryData\":\"\",\"SkinGeometryDataEngineVersion\":\"\",\"SkinAnimationData\":\"\",\"AnimatedImageData\":[],"
        "\"CapeData\":\"\",\"CapeId\":\"\",\"CapeImageWidth\":0,\"CapeImageHeight\":0,\"CapeOnClassicSkin\":false,"
        "\"PersonaPieces\":[],\"PieceTintColors\":[],\"PersonaSkin\":false,\"PremiumSkin\":false,\"ArmSize\":\"wide\",\"SkinColor\":\"#0\","
        "\"TrustedSkin\":false,\"OverrideSkin\":false,\"ClientRandomId\":1234,\"CurrentInputMode\":1,\"DefaultInputMode\":1,\"DeviceId\":\"capture-device\","
        "\"DeviceModel\":\"capture\",\"DeviceOS\":7,\"GameVersion\":\"1.26.65\",\"GuiScale\":0,\"IsEditorMode\":false,\"LanguageCode\":\"en_US\","
        "\"PlatformOfflineId\":\"\",\"PlatformOnlineId\":\"\",\"PlayFabId\":\"\",\"SelfSignedId\":\"11111111-2222-3333-4444-555555555555\","
        "\"ServerAddress\":\"127.0.0.1:" + std::to_string(port) + "\",\"UIProfile\":0,\"MaxViewDistance\":16,\"MemoryTier\":2,\"PlatformType\":0,"
        "\"GraphicsMode\":1,\"CompatibleWithClientSideChunkGen\":false}";
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> got;
    int seq = 0;
    std::FILE* index = std::fopen((outDir + "/index.txt").c_str(), "w");
    const auto pump = [&](int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            std::vector<std::vector<uint8_t>> batch;
            { std::lock_guard lock(mutex); batch.swap(messages); }
            for (auto& wire : batch) {
                std::vector<uint8_t> content;
                if (!codec.Open(wire, content)) { std::printf("cannot open batch (%zu bytes)\n", wire.size()); continue; }
                BatchCodec::Split(content, [&](uint32_t id, std::span<const uint8_t> body) {
                    char name[256];
                    std::snprintf(name, sizeof name, "%s/%04d_%u.bin", outDir.c_str(), seq++, id);
                    std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(body.data()), body.size());
                    std::fprintf(index, "%04d %u %zu\n", seq - 1, id, body.size());
                    got.emplace_back(id, std::vector<uint8_t>(body.begin(), body.end()));
                    if (id == proto::NetworkSettingsPacket::kId) {
                        proto::NetworkSettingsPacket ns;
                        net::ReadStream rs(body.data(), body.size());
                        if (Read(rs, ns)) codec.EnableCompression(ns.compressionThreshold);
                    }
                });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::fflush(index);
    };
    const auto seen = [&](uint32_t id) { for (auto& [i, b] : got) if (i == id) return true; return false; };
    pump(1500);
    proto::LoginPacket login;
    login.protocolVersion = 2225;
    {
        // A self-signed chain as an offline client makes it.
        const int64_t now = testauth::Now();
        const std::string self = testauth::Jwt(me.player, "{\"certificateAuthority\":true,\"exp\":" + std::to_string(now + 3600) +
                                                              ",\"identityPublicKey\":\"" + me.player.spki + "\",\"nbf\":" +
                                                              std::to_string(now - 60) +
                                                              ",\"extraData\":{\"XUID\":\"\",\"identity\":\"11111111-2222-3333-4444-555555555555\","
                                                              "\"displayName\":\"Capture\",\"titleId\":\"896928775\"}}");
        login.tokens.identity = me.Identity({self}, std::getenv("CAP_AUTH") ? std::atoi(std::getenv("CAP_AUTH")) : 2);
    }
    if (std::getenv("CAP_RAWCHAIN")) {
        std::string list;
        for (const std::string& c : me.Chain(false))
            list += (list.empty() ? "\"" : ",\"") + c + "\"";
        login.tokens.identity = "{\"chain\":[" + list + "]}";
    }
    login.tokens.client = testauth::Jwt(me.player, clientData);
    send(Wire(codec, login));
    pump(2000);
    if (seen(proto::ServerToClientHandshakePacket::kId)) { send(Wire(codec, proto::ClientToServerHandshakePacket{})); pump(1000); }
    proto::ResourcePackClientResponsePacket have;
    have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
    send(Wire(codec, have));
    pump(1000);
    proto::ResourcePackClientResponsePacket done;
    done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
    send(Wire(codec, done));
    pump(3000);
    proto::RequestChunkRadiusPacket radius;
    radius.chunkRadius = 4;
    radius.maxRadius = 4;
    send(Wire(codec, radius));
    pump(4000);
    proto::SetLocalPlayerAsInitializedPacket init;
    send(Wire(codec, init));
    pump(2000);
    std::vector<std::pair<double, std::string>> script;
    if (argc > 4) {
        std::ifstream in(argv[4]);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            const size_t sp = line.find(' ');
            if (sp == std::string::npos)
                continue;
            script.emplace_back(std::atof(line.substr(0, sp).c_str()), line.substr(sp + 1));
        }
    }
    const char* console = std::getenv("CAPTURE_CONSOLE");
    const auto start = std::chrono::steady_clock::now();
    size_t nextCommand = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        while (nextCommand < script.size() && script[nextCommand].first <= t) {
            if (console) {
                std::ofstream fifo(console);
                fifo << script[nextCommand].second << "\n";
            }
            std::fprintf(index, "# t=%.1f command: %s\n", t, script[nextCommand].second.c_str());
            nextCommand++;
        }
        pump(100);
    }
    // A readable summary of the entity packets.
    {
        std::FILE* sum = std::fopen((outDir + "/summary.txt").c_str(), "w");
        std::map<uint64_t, std::string> types;
        const auto meta = [&](const std::vector<proto::MetadataDictionaryEntry>& md) {
            std::string out;
            for (const auto& e : md) {
                out += " k" + std::to_string(static_cast<uint32_t>(e.key)) + "=";
                if (e.value.index() == 1 || e.value.index() == 2)
                    out += std::to_string(e.value.index() == 1 ? std::get<1>(e.value) : std::get<2>(e.value));
                else if (e.value.index() == 3)
                    out += std::to_string(std::get<3>(e.value));
                else if (e.value.index() == 4) {
                    const auto& v = std::get<4>(e.value);
                    std::visit([&](const auto& x) {
                        using T = std::decay_t<decltype(x)>;
                        if constexpr (std::is_arithmetic_v<T>)
                            out += std::to_string(x);
                        else if constexpr (std::is_same_v<T, std::string>)
                            out += "'" + x + "'";
                        else
                            out += "?";
                    }, v);
                }
            }
            return out;
        };
        for (auto& [id, body] : got) {
            net::ReadStream rs(body.data(), body.size());
            if (id == proto::AddEntityPacket::kId) {
                proto::AddEntityPacket p;
                if (!Read(rs, p)) continue;
                types[p.runtimeId] = p.entityType;
                std::fprintf(sum, "add %s id=%llu pos=%.2f,%.2f,%.2f vel=%.3f,%.3f,%.3f rot=%.1f,%.1f attrs=%zu meta:%s\n", p.entityType.c_str(),
                             (unsigned long long)p.runtimeId, p.position.x, p.position.y, p.position.z, p.velocity.x, p.velocity.y, p.velocity.z,
                             p.pitch, p.yaw, p.attributes.size(), meta(p.metadata).c_str());
            } else if (id == proto::SetEntityDataPacket::kId) {
                proto::SetEntityDataPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "data %s:%s\n", types[p.runtimeEntityId].c_str(), meta(p.metadata).c_str());
            } else if (id == proto::SetEntityMotionPacket::kId) {
                proto::SetEntityMotionPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "motion %s %.3f,%.3f,%.3f\n", types[p.runtimeEntityId].c_str(), p.velocity.x, p.velocity.y, p.velocity.z);
            } else if (id == proto::MoveEntityDeltaPacket::kId) {
                proto::MoveEntityDeltaPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "delta %s %s%s%s ground=%d\n", types[p.runtimeEntityId].c_str(), p.x ? "x" : "", p.y ? "y" : "", p.z ? "z" : "", p.onGround);
            } else if (id == proto::MoveEntityPacket::kId) {
                proto::MoveEntityPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "move %s flags=%u\n", types[p.runtimeEntityId].c_str(), (unsigned)p.flags);
            } else if (id == proto::MobEquipmentPacket::kId) {
                proto::MobEquipmentPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "equipment %s item=%d count=%u slot=%u sel=%u window=%d stackid=%d\n", types[p.runtimeEntityId].c_str(), p.item.networkId,
                             p.item.count, p.slot, p.selectedSlot, (int)p.windowId, p.item.hasStackId ? 1 : 0);
            } else if (id == proto::EntityEventPacket::kId) {
                proto::EntityEventPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "event %s %d data=%d\n", types[p.runtimeEntityId].c_str(), (int)p.eventId, p.data);
            } else if (id == proto::LevelSoundEventPacket::kId) {
                proto::LevelSoundEventPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "sound %s extra=%d type=%s\n", p.soundId.c_str(), p.extraData, p.entityType.c_str());
            } else if (id == proto::RemoveEntityPacket::kId) {
                proto::RemoveEntityPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "remove %s\n", types[static_cast<uint64_t>(p.entityIdSelf)].c_str());
            } else if (id == proto::TakeItemEntityPacket::kId) {
                proto::TakeItemEntityPacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "take %s\n", types[p.target].c_str());
            } else if (id == proto::MobArmorEquipmentPacket::kId) {
                std::fprintf(sum, "armor\n");
            } else if (id == proto::AnimatePacket::kId) {
                proto::AnimatePacket p;
                if (!Read(rs, p)) continue;
                std::fprintf(sum, "animate %s %d\n", types[p.runtimeEntityId].c_str(), (int)p.actionId);
            }
        }
        std::fclose(sum);
    }
    std::fclose(index);
    std::printf("captured %d packets into %s\n", seq, outDir.c_str());
    pc->close();
    return 0;
}
