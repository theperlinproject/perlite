#include "test_util.hpp"
#include "test_auth.hpp"

#include "common/Json.hpp"
#include "server/Server.hpp"
#include "network/Batch.hpp"

#include "protocol/Packets.hpp"

#include "support/MiniHttp.hpp"
#include "network/nethernet/LanDiscovery.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <rtc/rtc.hpp>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

#include <atomic>
#include <set>
#include <sstream>
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
    namespace lan = perlite::nethernet::lan;

    // A stand-in for a Bedrock client on the LAN: broadcasts discovery and signals over UDP 7551.
    struct LanClient {
#ifdef _WIN32
        using Socket = SOCKET;
        static int Poll(Socket s, int ms) {
            WSAPOLLFD p{};
            p.fd = s;
            p.events = POLLRDNORM;
            return WSAPoll(&p, 1, ms);
        }
        static void CloseSocket(Socket s) { closesocket(s); }
#else
        using Socket = int;
        static int Poll(Socket s, int ms) {
            pollfd p{s, POLLIN, 0};
            return ::poll(&p, 1, ms);
        }
        static void CloseSocket(Socket s) { ::close(s); }
#endif
        Socket sock{};
        std::set<std::string> seen;
        uint64_t id = 0x1122334455667788ull;
        uint64_t server = 0;
        std::string serverData;
        sockaddr_in replyTo{};
        bool haveReplyTo = false;
        // sharePort: bind port 7551 as well, like a game running on the server's machine.
        bool shared = false;
        explicit LanClient(bool sharePort = false) : shared(sharePort) {
#ifdef _WIN32
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
            sock = ::socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(sharePort ? INADDR_ANY : INADDR_LOOPBACK);
            if (sharePort) {
                int yes = 1;
                ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
                ::setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
                ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
                a.sin_port = htons(lan::kPort);
            }
            ::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        }
        ~LanClient() { CloseSocket(sock); }
        void Send(uint16_t type, const std::string& body) {
            const std::string d = lan::Encode(type, id, body);
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(lan::kPort);
            to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (type == lan::kMessage && haveReplyTo)
                to = replyTo; // signals go back to where the server's response came from
            ::sendto(sock, d.data(), static_cast<int>(d.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
        void Signal(const std::string& text) { Send(lan::kMessage, lan::MessageBody(server, text)); }
        // Next packet from the server, or false after timeoutMs.
        bool Receive(uint16_t& type, std::string& body, int timeoutMs) {
            std::string buf(65536, '\0');
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < end) {
                if (Poll(sock, 50) <= 0)
                    continue;
                sockaddr_in from{};
                socklen_t fromLen = sizeof(from);
                const int n = static_cast<int>(::recvfrom(sock, buf.data(), static_cast<int>(buf.size()), 0,
                                                          reinterpret_cast<sockaddr*>(&from), &fromLen));
                uint64_t sender = 0;
                if (n > 0 && lan::Decode(buf.substr(0, static_cast<size_t>(n)), type, sender, body) && sender != id) {
                    if (type == lan::kMessage) {
                        uint64_t recipient = 0;
                        std::string text;
                        if (!lan::ParseMessageBody(body, recipient, text) || recipient != id)
                            continue;
                        // The server may send a message twice (unicast and broadcast); take it once.
                        if (!seen.insert(text).second)
                            continue;
                    }
                    if (type == lan::kResponse) {
                        replyTo = from;
                        haveReplyTo = true;
                    }
                    server = sender;
                    return true;
                }
            }
            return false;
        }
        bool Discover() {
            Send(lan::kRequest, {});
            if (shared) {
                // The game broadcasts its search. On a shared port a unicast copy may even come
                // back to our own socket (Linux spreads unicast over SO_REUSEPORT sockets).
                const std::string d = lan::Encode(lan::kRequest, id, {});
                sockaddr_in to{};
                to.sin_family = AF_INET;
                to.sin_port = htons(lan::kPort);
                to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
                ::sendto(sock, d.data(), static_cast<int>(d.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
            }
            uint16_t type = 0;
            std::string body;
            while (Receive(type, body, 2000))
                if (type == lan::kResponse)
                    return lan::ParseResponseBody(body, serverData);
            return false;
        }
    };

    // Whether a broadcast sent from this host is delivered back to a socket on this host.
    bool BroadcastLoopsBack() {
#ifdef _WIN32
        using Sock = SOCKET;
        const auto closeSock = [](Sock s) { closesocket(s); };
#else
        using Sock = int;
        const auto closeSock = [](Sock s) { ::close(s); };
#endif
        Sock rx = ::socket(AF_INET, SOCK_DGRAM, 0), tx = ::socket(AF_INET, SOCK_DGRAM, 0);
        int yes = 1;
        ::setsockopt(tx, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        ::bind(rx, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t len = sizeof(a);
        ::getsockname(rx, reinterpret_cast<sockaddr*>(&a), &len);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = a.sin_port;
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        bool got = false;
        for (int attempt = 0; attempt < 3 && !got; attempt++) {
            ::sendto(tx, "ping", 4, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
            got = LanClient::Poll(rx, 300) > 0;
        }
        closeSock(rx);
        closeSock(tx);
        return got;
    }

    struct Outcome {
        bool connected = false;
        bool loginSuccess = false;
        bool disconnected = false;
        int disconnectReason = -1;
        bool gotChunk = false;
        bool spawned = false;
    };

    Outcome JoinOver(uint16_t port, const std::string& identity, const std::string& clientToken,
                     const testauth::Key* assertWith, bool fullHandshake, bool overLan = false,
                     bool sharePort = false) {
        Outcome out;
        auto probe = minihttp::Request(port, "GET", "/v1/join?version=1.26.60&protocol=2225&platform=1&id=7");
        CHECK(probe && probe->status == 200 && probe->contentType == "application/json");
        if (probe) {
            json::Value v;
            CHECK(json::Parse(probe->body, v) && v["maxPlayers"].Int() > 0 && !v["name"].String().empty());
        }
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
        if (overLan) {
            // Vanilla trickles: the offer goes out bare and the candidates follow one by one.
            LanClient client(sharePort);
            const std::string conn = sharePort ? "5353" : "4242";
            CHECK(client.Discover());
            std::string bare, candidates;
            std::vector<std::string> lines;
            {
                std::istringstream in(offer);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.starts_with("a=candidate:"))
                        lines.push_back(line.substr(2) + " generation 0 ufrag x network-id 1 network-cost 10");
                    else if (line != "a=end-of-candidates")
                        bare += line + "\r\n";
                }
            }
            client.Signal("CONNECTREQUEST " + conn + " " + bare);
            for (const auto& c : lines)
                client.Signal("CANDIDATEADD " + conn + " " + c);
            bool answered = false;
            uint16_t type = 0;
            std::string body;
            for (int i = 0; i < 100 && !open; i++) {
                if (!client.Receive(type, body, 100) || type != lan::kMessage)
                    continue;
                uint64_t to = 0;
                std::string text;
                if (!lan::ParseMessageBody(body, to, text) || to != client.id)
                    continue;
                if (text.starts_with("CONNECTRESPONSE " + conn + " ")) {
                    const std::string sdp = text.substr(17 + conn.size());
                    // LAN answers carry no identity line, like the offers.
                    CHECK(!sdp.contains("a=identity"));
                    pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
                    answered = true;
                } else if (text.starts_with("CANDIDATEADD " + conn + " ") && answered) {
                    pc->addRemoteCandidate(rtc::Candidate(text.substr(14 + conn.size()), "0"));
                } else if (text.starts_with("CONNECTERROR")) {
                    std::fprintf(stderr, "lan error: %s\n", text.c_str());
                    break;
                }
            }
            CHECK(answered);
            for (int i = 0; i < 200 && !open; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK(open);
            out.connected = open;
            if (!open)
                return out;
        } else {
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
        }

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
            radius.chunkRadius = 8;
            radius.maxRadius = 8;
            send(Wire(codec, radius));
            out.gotChunk = waitFor(proto::LevelChunkPacket::kId);
            CHECK(out.gotChunk);
            // A full spawn pushes several hundred KB through the SCTP send queue, which is what a
            // real client sees; the session must stay up until PlayerSpawn arrives.
            bool spawned = false;
            for (int i = 0; i < 200 && !spawned; i++) {
                if (!waitFor(proto::PlayStatusPacket::kId, &body, 1))
                    continue;
                proto::PlayStatusPacket st;
                net::ReadStream rs(body.data(), body.size());
                spawned = Read(rs, st) && st.status == proto::PlayStatusPacketStatus::PlayerSpawn;
            }
            out.spawned = spawned;
            CHECK(spawned);
        }
        pc->close();
        return out;
    }
}

int main() {
    // Loopback drains SCTP instantly; a small send buffer makes libdatachannel queue messages the
    // way it does on a real network, where send() then returns false for buffered data.
    {
        rtc::SctpSettings sctp;
        sctp.sendBufferSize = 64 * 1024;
        rtc::SetSctpSettings(sctp);
    }
    {
        // Byte-for-byte the ServerData df-mc/go-nethernet expects (server_data_test.go).
        lan::ServerData d;
        d.serverName = "server";
        d.protocol = 2181;
        d.version = "1.26.50";
        d.levelName = "world";
        d.gameType = 2;
        d.playerCount = 1;
        d.maxPlayerCount = 8;
        d.hardcore = true;
        d.acceptsOnlineAuth = true;
        d.acceptsSelfSignedAuth = true;
        d.nonce = "nonce";
        d.connectionType = 4;
        static const char kWant[] = "\x07\x06server\x8a\x22\x07" "1.26.50\x05world\x02\x10\x04\x00\x01\x01\x01\x05nonce\x08";
        const std::string want(kWant, sizeof(kWant) - 1);
        CHECK(want.size() == 38);
        CHECK(lan::EncodeServerData(d) == want);
        lan::ServerData back;
        CHECK(lan::DecodeServerData(want, back) && back.serverName == "server" && back.protocol == 2181 &&
              back.gameType == 2 && back.maxPlayerCount == 8 && back.nonce == "nonce");
        uint16_t type = 0;
        uint64_t sender = 0;
        std::string body;
        const std::string wire = lan::Encode(lan::kMessage, 77, lan::MessageBody(5, std::string(3000, 'x')));
        CHECK(lan::Decode(wire, type, sender, body) && type == lan::kMessage && sender == 77);
        uint64_t to = 0;
        std::string text;
        CHECK(lan::ParseMessageBody(body, to, text) && to == 5 && text.size() == 3000);
        std::string tampered = wire;
        tampered[40] ^= 1;
        CHECK(!lan::Decode(tampered, type, sender, body));
    }
    constexpr uint16_t kPort = 19333;
    ServerConfig config;
    config.port = kPort;
    config.bindAddress = "127.0.0.1";
    config.generator = std::getenv("PS_TEST_VANILLA") ? "vanilla" : "flat";
    config.terrainBackend = "cpu";
    config.iceServers = {"none"};
    config.iceBindAddress = "127.0.0.1";
    config.iceUdpMux = std::getenv("PS_TEST_MUX") != nullptr;
    config.networkEncryption = false;
    config.viewDistance = 8;
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

        // LAN: the world is advertised on 7551 and joinable through UDP signalling alone.
        LanClient finder;
        CHECK(finder.Discover());
        lan::ServerData d;
        CHECK(lan::DecodeServerData(finder.serverData, d));
        CHECK(d.serverName == config.name && d.protocol == 2225 && d.version == "1.26.60");
        CHECK(d.connectionType == 4 && d.maxPlayerCount == config.maxPlayers && d.acceptsSelfSignedAuth);
        const Outcome viaLan =
            JoinOver(kPort, guest.Identity(guest.Chain(false), 2), guest.ClientToken(), nullptr, true, true);
        CHECK(viaLan.connected && viaLan.loginSuccess && viaLan.spawned);
        // The same from a game on this machine, which listens on port 7551 too. On a shared port the
        // system may hand a unicast datagram to either socket, and only broadcasts reach both, so
        // this needs broadcasts to come back to this host (some CI containers drop them).
        if (BroadcastLoopsBack()) {
            const Outcome sameMachine =
                JoinOver(kPort, guest.Identity(guest.Chain(false), 2), guest.ClientToken(), nullptr, true, true, true);
            CHECK(sameMachine.connected && sameMachine.loginSuccess && sameMachine.spawned);
        } else {
            std::puts("server_nethernet_test: broadcasts do not reach this host, skipping the same-machine LAN join");
        }
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
