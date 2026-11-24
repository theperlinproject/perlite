// A player connected through the loopback listener, for the tests that drive a whole server.
#pragma once
#include "test_auth.hpp"

#include "network/Batch.hpp"
#include "protocol/Packets.hpp"
#include "server/Server.hpp"

#include <chrono>
#include <thread>

namespace perlite::testclient {
    namespace proto = perlite::proto2225;
    namespace net = perlite::net;
    inline std::string B64Url(const std::string& s) {
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

    inline std::string Jwt(const std::string& payload) {
        return B64Url("{\"alg\":\"none\"}") + "." + B64Url(payload) + ".sig";
    }

    struct Received {
        uint32_t id;
        std::vector<uint8_t> body;
    };

    class Client {
    public:
        Client(Server& server, std::string name)
            : server(server), name(name), endpoint(server.Loopback().Connect()), account(name, "") {}

        template <class P> void Send(const P& packet) {
            std::vector<uint8_t> body;
            net::EncodeChecked(packet, body);
            std::vector<uint8_t> content;
            BatchCodec::AppendPacket(content, P::kId, body);
            const std::vector<uint8_t> wire = codec.Seal(content);
            endpoint->Send(wire.data(), wire.size());
        }

        void Settle(int ticks = 3) {
            for (int i = 0; i < ticks; i++) {
                server.Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Drain();
        }

        // Ticks until a packet of type P has arrived (or the limit passes): for checks whose answer
        // can take a few ticks on a slow or busy machine.
        template <class P> bool SettleFor(int maxTicks = 200) {
            for (int i = 0; i < maxTicks; i++) {
                Drain();
                for (const Received& r : inbox)
                    if (r.id == P::kId)
                        return true;
                server.Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Drain();
            return false;
        }

        void Drain() {
            std::vector<uint8_t> wire;
            while (endpoint->Receive(wire)) {
                std::vector<uint8_t> content;
                if (!codec.Open(wire, content)) {
                    corrupt = true;
                    continue;
                }
                BatchCodec::Split(content, [&](uint32_t id, std::span<const uint8_t> body) {
                    inbox.push_back({id, std::vector<uint8_t>(body.begin(), body.end())});
                    if (id == proto::NetworkSettingsPacket::kId)
                        codec.EnableCompression(256);
                });
            }
        }

        template <class P> std::vector<P> Take() {
            std::vector<P> out;
            for (auto it = inbox.begin(); it != inbox.end();) {
                if (it->id == P::kId) {
                    P p;
                    net::ReadStream s(it->body.data(), it->body.size());
                    if (Read(s, p) && s.ok)
                        out.push_back(std::move(p));
                    else
                        malformed++;
                    it = inbox.erase(it);
                } else {
                    ++it;
                }
            }
            return out;
        }

        void Login(int protocol = 2225) {
            proto::RequestNetworkSettingsPacket req;
            req.clientProtocol = protocol;
            Send(req);
            Settle();
            if (protocol != 2225)
                return;
            proto::LoginPacket login;
            login.protocolVersion = protocol;
            login.tokens.identity = account.Identity(account.Chain(false), 2);
            login.tokens.client = account.ClientToken();
            Send(login);
            Settle();
        }

        Server& server;
        std::string name;
        std::shared_ptr<LoopbackListener::Endpoint> endpoint;
        testauth::Account account;
        BatchCodec codec;
        std::vector<Received> inbox;
        bool corrupt = false;
        int malformed = 0;
    };

    inline void Join(Client& c, int radius = 4) {
        c.Login();
        proto::ResourcePackClientResponsePacket have;
        have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
        c.Send(have);
        c.Settle();
        proto::ResourcePackClientResponsePacket done;
        done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
        c.Send(done);
        c.Settle();
        proto::RequestChunkRadiusPacket r;
        r.chunkRadius = radius;
        r.maxRadius = static_cast<uint8_t>(radius);
        c.Send(r);
        for (int i = 0; i < 60; i++)
            c.Settle(2);
    }
}
