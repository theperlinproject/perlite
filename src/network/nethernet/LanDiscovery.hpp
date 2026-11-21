#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace perlite::nethernet {

    // NetherNet LAN discovery, the way Bedrock 26.60+ clients find worlds under Friends > LAN Games.
    // Clients broadcast a request on UDP 7551 every two seconds; hosts answer with their ServerData.
    // When a player picks the world, the client signals the WebRTC offer, answer and ICE candidates
    // through message packets on the same port (CONNECTREQUEST / CONNECTRESPONSE / CANDIDATEADD).
    //
    // Every packet is HMAC-SHA256(payload) followed by AES-256-ECB(payload, PKCS#7), keyed with
    // SHA-256 of the little-endian uint64 0xdeadbeef.
    namespace lan {
        constexpr uint16_t kPort = 7551;
        constexpr uint16_t kRequest = 0;
        constexpr uint16_t kResponse = 1;
        constexpr uint16_t kMessage = 2;
        constexpr uint8_t kServerDataVersion = 7;
        constexpr int32_t kConnectionTypeLan = 4;

        struct ServerData {
            std::string serverName;
            int32_t protocol = 0;
            std::string version;
            std::string levelName;
            int32_t gameType = 0;
            int32_t playerCount = 0;
            int32_t maxPlayerCount = 0;
            bool editorWorld = false;
            bool hardcore = false;
            bool acceptsOnlineAuth = true;
            bool acceptsSelfSignedAuth = true;
            std::string nonce;
            int32_t connectionType = kConnectionTypeLan;
        };

        std::string EncodeServerData(const ServerData& d);
        bool DecodeServerData(const std::string& bytes, ServerData& out);

        // Whole datagram for a packet. body is everything after the 20-byte header.
        std::string Encode(uint16_t id, uint64_t sender, const std::string& body);
        bool Decode(const std::string& datagram, uint16_t& id, uint64_t& sender, std::string& body);

        std::string ResponseBody(const std::string& serverData);
        bool ParseResponseBody(const std::string& body, std::string& serverData);
        std::string MessageBody(uint64_t recipient, const std::string& data);
        bool ParseMessageBody(const std::string& body, uint64_t& recipient, std::string& data);
    }

    class LanDiscovery {
    public:
        struct Callbacks {
            // Binary ServerData to advertise; empty hides the server.
            std::function<std::string()> serverData;
            // A signal ("TYPE CONNECTIONID DATA") addressed to this server.
            std::function<void(uint64_t sender, const std::string& address, const std::string& signal)> signal;
        };

        ~LanDiscovery();

        bool Start(uint16_t port, uint64_t networkId, Callbacks callbacks, std::string& error);
        void Stop();
        bool Running() const { return running; }
        uint64_t NetworkId() const { return networkId; }

        // Sends a signal to the peer that last messaged us with that network id.
        bool Signal(uint64_t recipient, const std::string& text);

    private:
        struct PeerAddress {
            std::string raw;
            std::chrono::steady_clock::time_point seen;
        };

        void Loop();
        void Handle(const std::string& datagram, const std::string& fromRaw);
        void SendTo(const std::string& datagram, const std::string& toRaw);
        void Broadcast(const std::string& datagram);

        intptr_t sock = -1;
        uint16_t port = lan::kPort;
        uint64_t networkId = 0;
        Callbacks callbacks;
        std::atomic<bool> running{false};
        std::thread thread;
        std::mutex mutex;
        std::unordered_map<uint64_t, PeerAddress> addresses;
    };
}
