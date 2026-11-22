#include "network/nethernet/LanDiscovery.hpp"

#include "common/Log.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define PERLITE_CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define PERLITE_CLOSE_SOCKET ::close
#endif

namespace perlite::nethernet {
    namespace lan {
        namespace {
            constexpr size_t kHeaderBytes = 20; // length, id, sender, 8 zero bytes
            constexpr size_t kMessageHeaderBytes = 32;
            constexpr size_t kMaxDeclaredMessage = 1140;

            const std::array<unsigned char, 32>& Key() {
                static const std::array<unsigned char, 32> key = [] {
                    std::array<unsigned char, 32> k{};
                    unsigned char seed[8];
                    uint64_t v = 0xdeadbeef;
                    for (int i = 0; i < 8; i++)
                        seed[i] = static_cast<unsigned char>(v >> (8 * i));
                    SHA256(seed, sizeof(seed), k.data());
                    return k;
                }();
                return key;
            }

            std::string Hmac(const std::string& data) {
                unsigned char out[32];
                unsigned int len = 0;
                HMAC(EVP_sha256(), Key().data(), static_cast<int>(Key().size()),
                     reinterpret_cast<const unsigned char*>(data.data()), data.size(), out, &len);
                return std::string(reinterpret_cast<char*>(out), len);
            }

            bool Crypt(bool encrypt, const std::string& in, std::string& out) {
                EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
                if (!ctx)
                    return false;
                out.assign(in.size() + 32, '\0');
                int n1 = 0, n2 = 0;
                const bool ok =
                    EVP_CipherInit_ex(ctx, EVP_aes_256_ecb(), nullptr, Key().data(), nullptr, encrypt ? 1 : 0) == 1 &&
                    EVP_CipherUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &n1,
                                     reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size())) ==
                        1 &&
                    EVP_CipherFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + n1, &n2) == 1;
                EVP_CIPHER_CTX_free(ctx);
                if (!ok)
                    return false;
                out.resize(static_cast<size_t>(n1 + n2));
                return true;
            }

            void PutLe(std::string& o, uint64_t v, int bytes) {
                for (int i = 0; i < bytes; i++)
                    o += static_cast<char>(v >> (8 * i));
            }
            uint64_t GetLe(const std::string& s, size_t at, int bytes) {
                uint64_t v = 0;
                for (int i = 0; i < bytes; i++)
                    v |= static_cast<uint64_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
                return v;
            }

            void PutVarUint(std::string& o, uint32_t v) {
                while (v >= 0x80) {
                    o += static_cast<char>((v & 0x7f) | 0x80);
                    v >>= 7;
                }
                o += static_cast<char>(v);
            }
            void PutVarInt(std::string& o, int32_t v) {
                PutVarUint(o, (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
            }
            void PutString(std::string& o, const std::string& s) {
                PutVarUint(o, static_cast<uint32_t>(s.size()));
                o += s;
            }

            struct Reader {
                const std::string& s;
                size_t at = 0;
                bool ok = true;
                uint32_t VarUint() {
                    uint32_t v = 0;
                    for (int shift = 0; shift < 35; shift += 7) {
                        if (at >= s.size()) {
                            ok = false;
                            return 0;
                        }
                        const auto b = static_cast<unsigned char>(s[at++]);
                        v |= static_cast<uint32_t>(b & 0x7f) << shift;
                        if (!(b & 0x80))
                            return v;
                    }
                    ok = false;
                    return 0;
                }
                int32_t VarInt() {
                    const uint32_t u = VarUint();
                    return static_cast<int32_t>((u >> 1) ^ (~(u & 1) + 1));
                }
                std::string String() {
                    const uint32_t n = VarUint();
                    if (!ok || n > s.size() - at) {
                        ok = false;
                        return {};
                    }
                    std::string out = s.substr(at, n);
                    at += n;
                    return out;
                }
                bool Bool() {
                    if (at >= s.size()) {
                        ok = false;
                        return false;
                    }
                    return s[at++] != 0;
                }
            };
        }

        std::string EncodeServerData(const ServerData& d) {
            std::string o;
            o += static_cast<char>(kServerDataVersion);
            PutString(o, d.serverName);
            PutVarInt(o, d.protocol);
            PutString(o, d.version);
            PutString(o, d.levelName);
            PutVarInt(o, d.playerCount);
            PutVarInt(o, d.maxPlayerCount);
            PutVarInt(o, d.gameType);
            o += static_cast<char>(d.editorWorld ? 1 : 0);
            o += static_cast<char>(d.hardcore ? 1 : 0);
            o += static_cast<char>(d.acceptsOnlineAuth ? 1 : 0);
            o += static_cast<char>(d.acceptsSelfSignedAuth ? 1 : 0);
            PutString(o, d.nonce);
            PutVarInt(o, d.connectionType);
            return o;
        }

        bool DecodeServerData(const std::string& bytes, ServerData& d) {
            if (bytes.empty() || static_cast<uint8_t>(bytes[0]) != kServerDataVersion)
                return false;
            Reader r{bytes, 1};
            d.serverName = r.String();
            d.protocol = r.VarInt();
            d.version = r.String();
            d.levelName = r.String();
            d.playerCount = r.VarInt();
            d.maxPlayerCount = r.VarInt();
            d.gameType = r.VarInt();
            d.editorWorld = r.Bool();
            d.hardcore = r.Bool();
            d.acceptsOnlineAuth = r.Bool();
            d.acceptsSelfSignedAuth = r.Bool();
            d.nonce = r.String();
            d.connectionType = r.VarInt();
            return r.ok && r.at == bytes.size();
        }

        std::string Encode(uint16_t id, uint64_t sender, const std::string& body) {
            std::string rest;
            PutLe(rest, id, 2);
            PutLe(rest, sender, 8);
            rest.append(8, '\0');
            rest += body;
            // The length counts the whole decrypted payload, itself included. Vanilla caps the data
            // length it declares for messages at 1140 bytes but still sends all of it.
            size_t length = rest.size() + 2;
            if (id == kMessage && body.size() >= 12)
                length = kMessageHeaderBytes + std::min(body.size() - 12, kMaxDeclaredMessage);
            std::string payload;
            PutLe(payload, static_cast<uint16_t>(length), 2);
            payload += rest;
            std::string encrypted;
            if (!Crypt(true, payload, encrypted))
                return {};
            return Hmac(payload) + encrypted;
        }

        bool Decode(const std::string& datagram, uint16_t& id, uint64_t& sender, std::string& body) {
            if (datagram.size() < 32 + 16 || (datagram.size() - 32) % 16 != 0)
                return false;
            std::string payload;
            if (!Crypt(false, datagram.substr(32), payload))
                return false;
            const std::string mac = Hmac(payload);
            if (CRYPTO_memcmp(mac.data(), datagram.data(), 32) != 0)
                return false;
            if (payload.size() < kHeaderBytes || payload.size() < GetLe(payload, 0, 2))
                return false;
            id = static_cast<uint16_t>(GetLe(payload, 2, 2));
            sender = GetLe(payload, 4, 8);
            body = payload.substr(kHeaderBytes);
            return true;
        }

        std::string ResponseBody(const std::string& serverData) {
            static const char* hex = "0123456789abcdef";
            std::string text;
            text.reserve(serverData.size() * 2);
            for (unsigned char c : serverData) {
                text += hex[c >> 4];
                text += hex[c & 15];
            }
            std::string o;
            PutLe(o, text.size(), 4);
            return o + text;
        }

        bool ParseResponseBody(const std::string& body, std::string& serverData) {
            if (body.size() < 4 || (body.size() - 4) % 2 != 0)
                return false;
            const auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            serverData.clear();
            for (size_t i = 4; i < body.size(); i += 2) {
                const int hi = nibble(body[i]), lo = nibble(body[i + 1]);
                if (hi < 0 || lo < 0)
                    return false;
                serverData += static_cast<char>(hi << 4 | lo);
            }
            return true;
        }

        std::string MessageBody(uint64_t recipient, const std::string& data) {
            std::string o;
            PutLe(o, recipient, 8);
            PutLe(o, std::min(data.size(), kMaxDeclaredMessage), 4);
            return o + data;
        }

        bool ParseMessageBody(const std::string& body, uint64_t& recipient, std::string& data) {
            if (body.size() < 12)
                return false;
            recipient = GetLe(body, 0, 8);
            data = body.substr(12); // vanilla ignores the declared length and reads to the end
            return true;
        }
    }

    namespace {
        SocketHandle Sock(intptr_t s) {
            return static_cast<SocketHandle>(s);
        }
        bool Valid(intptr_t s) {
#ifdef _WIN32
            return Sock(s) != INVALID_SOCKET;
#else
            return s >= 0;
#endif
        }
        std::string Text(const std::string& raw) {
            if (raw.size() < sizeof(sockaddr_in))
                return "?";
            sockaddr_in a{};
            std::memcpy(&a, raw.data(), sizeof(a));
            char host[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &a.sin_addr, host, sizeof(host));
            return std::string(host) + ":" + std::to_string(ntohs(a.sin_port));
        }
    }

    LanDiscovery::~LanDiscovery() {
        Stop();
    }

    bool LanDiscovery::Start(uint16_t p, uint64_t id, Callbacks cb, std::string& error) {
#ifdef _WIN32
        static const bool started = [] {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        (void)started;
#endif
        port = p;
        networkId = id;
        callbacks = std::move(cb);
        const SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!Valid(static_cast<intptr_t>(s))) {
            error = "lan discovery: cannot open a udp socket";
            return false;
        }
        int yes = 1;
        // Shared with a Minecraft client on the same machine, which binds 7551 too.
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
        ::setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
        ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_ANY); // broadcasts only reach a wildcard bind
        if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
            PERLITE_CLOSE_SOCKET(s);
            error = "lan discovery: cannot listen on udp " + std::to_string(port) + " (port in use?)";
            return false;
        }
        sock = static_cast<intptr_t>(s);
        // Everything we send goes out of a second socket on a port of its own, so the game's replies
        // (sent back to where our response came from) reach us even when a game on this machine
        // shares port 7551 with the listening socket.
        const SocketHandle r = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (Valid(static_cast<intptr_t>(r))) {
            ::setsockopt(r, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
            sockaddr_in any{};
            any.sin_family = AF_INET;
            any.sin_port = 0;
            any.sin_addr.s_addr = htonl(INADDR_ANY);
            if (::bind(r, reinterpret_cast<sockaddr*>(&any), sizeof(any)) == 0)
                replySock = static_cast<intptr_t>(r);
            else
                PERLITE_CLOSE_SOCKET(r);
        }
        running = true;
        thread = std::thread([this] {
            SetLogThreadName("LAN");
            Loop();
        });
        return true;
    }

    void LanDiscovery::Stop() {
        if (!running.exchange(false))
            return;
        if (thread.joinable())
            thread.join();
        if (Valid(sock))
            PERLITE_CLOSE_SOCKET(Sock(sock));
        sock = -1;
        if (Valid(replySock))
            PERLITE_CLOSE_SOCKET(Sock(replySock));
        replySock = -1;
    }

    void LanDiscovery::Loop() {
        std::string buf(65536, '\0');
        while (running) {
            const intptr_t socks[2] = {sock, replySock};
            const int count = Valid(replySock) ? 2 : 1;
#ifdef _WIN32
            WSAPOLLFD pfd[2]{};
            for (int i = 0; i < count; i++) {
                pfd[i].fd = Sock(socks[i]);
                pfd[i].events = POLLRDNORM;
            }
            if (WSAPoll(pfd, static_cast<ULONG>(count), 250) <= 0)
                continue;
#else
            pollfd pfd[2]{};
            for (int i = 0; i < count; i++) {
                pfd[i].fd = Sock(socks[i]);
                pfd[i].events = POLLIN;
            }
            if (::poll(pfd, static_cast<nfds_t>(count), 250) <= 0)
                continue;
#endif
            for (int i = 0; i < count; i++) {
                if (!(pfd[i].revents & (POLLIN | POLLRDNORM)))
                    continue;
                sockaddr_in from{};
                socklen_t len = sizeof(from);
                const int n = static_cast<int>(::recvfrom(Sock(socks[i]), buf.data(), static_cast<int>(buf.size()), 0,
                                                          reinterpret_cast<sockaddr*>(&from), &len));
                if (n <= 0)
                    continue;
                try {
                    Handle(buf.substr(0, static_cast<size_t>(n)),
                           std::string(reinterpret_cast<const char*>(&from), sizeof(from)));
                } catch (const std::exception& e) {
                    LogWarn(std::string("lan discovery: ") + e.what());
                }
            }
        }
    }

    void LanDiscovery::Handle(const std::string& datagram, const std::string& fromRaw) {
        uint16_t id = 0;
        uint64_t sender = 0;
        std::string body;
        if (!lan::Decode(datagram, id, sender, body))
            return;
        if (sender == networkId) {
            // A game on this machine shares port 7551 with us, and the system handed our own unicast
            // message to our socket instead of the game's. Send it again as a broadcast, which every
            // socket on the port gets (other games ignore it: messages name their recipient). The
            // broadcast copy comes back here too and is dropped.
            if (id != lan::kMessage)
                return;
            std::lock_guard lock(mutex);
            if (const auto it = rebroadcast.find(datagram); it != rebroadcast.end()) {
                rebroadcast.erase(it);
                return;
            }
            if (rebroadcast.size() > 256)
                rebroadcast.clear();
            rebroadcast.insert(datagram);
            Broadcast(datagram);
            return;
        }
        if (id != lan::kRequest && sender != 0) {
            std::lock_guard lock(mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(addresses, [&](const auto& e) { return now - e.second.seen > std::chrono::minutes(1); });
            addresses[sender] = {fromRaw, now};
        }
        if (id == lan::kRequest) {
            const std::string data = callbacks.serverData ? callbacks.serverData() : std::string();
            if (data.empty())
                return;
            const std::string reply = lan::Encode(lan::kResponse, networkId, lan::ResponseBody(data));
            SendTo(reply, fromRaw);
            // A client on this machine shares port 7551 with us, so a unicast reply can land on our
            // own socket; the broadcast copy reaches every socket bound to the port.
            Broadcast(reply);
            return;
        }
        if (id == lan::kMessage) {
            uint64_t recipient = 0;
            std::string text;
            if (!lan::ParseMessageBody(body, recipient, text) || recipient != networkId)
                return;
            if (text.empty() || text == "Ping")
                return;
            if (callbacks.signal)
                callbacks.signal(sender, Text(fromRaw), text);
        }
    }

    bool LanDiscovery::Signal(uint64_t recipient, const std::string& text) {
        std::string to;
        {
            std::lock_guard lock(mutex);
            const auto it = addresses.find(recipient);
            if (it == addresses.end())
                return false;
            to = it->second.raw;
        }
        const std::string datagram = lan::Encode(lan::kMessage, networkId, lan::MessageBody(recipient, text));
        if (datagram.empty())
            return false;
        SendTo(datagram, to);
        return true;
    }

    void LanDiscovery::SendTo(const std::string& datagram, const std::string& toRaw) {
        const intptr_t out = Valid(replySock) ? replySock : sock;
        if (!Valid(out) || datagram.empty())
            return;
        ::sendto(Sock(out), datagram.data(), static_cast<int>(datagram.size()), 0,
                 reinterpret_cast<const sockaddr*>(toRaw.data()), static_cast<socklen_t>(toRaw.size()));
    }

    void LanDiscovery::Broadcast(const std::string& datagram) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        SendTo(datagram, std::string(reinterpret_cast<const char*>(&a), sizeof(a)));
    }
}
