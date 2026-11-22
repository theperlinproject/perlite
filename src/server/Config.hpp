#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlite {

    struct FlatLayer {
        std::string block;
        int count = 1;
    };

    struct ServerConfig {
        std::string name = "Perlite Server";
        std::string motd = "A Perlite server";
        std::string levelName = "world";
        std::string dataDir = ".";
        bool xboxAuth = false;
        std::vector<std::string> authRootKeys;
        std::string pluginDir;
        std::string blockDataDir;
        uint32_t maxPlayers = 20;
        uint32_t viewDistance = 12;
        uint32_t tickRate = 20;
        int gameMode = 0;
        uint64_t seed = 0;
        std::vector<std::string> operators;
        bool whitelist = false;
        bool lanDiscovery = true;

        bool nethernet = true;
        uint16_t port = 19132;
        std::string bindAddress = "0.0.0.0";
        std::vector<std::string> iceServers;
        bool iceUdpMux = true;
        std::string iceBindAddress;

        std::string generator = "vanilla";
        std::vector<FlatLayer> flatLayers = {
            {"minecraft:bedrock", 1}, {"minecraft:dirt", 2}, {"minecraft:grass_block", 1}};
        std::string terrainBackend = "auto";

        bool loopbackOnly = false;

        bool randomSeed = false;
        std::string levelSeedText;
        int difficulty = 1;
        bool forceGamemode = false;
        bool allowCheats = false;
        std::string transport = "nethernet";
        uint16_t iceUdpPort = 0;
        bool udpPortsSet = false;
        uint16_t udpPortBegin = 0, udpPortEnd = 0;
        std::string udpPublicAddress;
        uint16_t udpPublicBegin = 0, udpPublicEnd = 0;
        uint16_t portV6 = 19133;
        uint32_t tickDistance = 4;
        std::vector<uint32_t> debugSkipPackets;
        bool debugLogPackets = false;
        uint32_t idleTimeoutMinutes = 30;
        uint32_t maxThreads = 8;
        std::string defaultPermission = "member";
        uint16_t compressionThreshold = 1;
        std::string compressionAlgorithm = "zlib";
        bool clientSideChunkGeneration = true;
        bool blockNetworkIdsAreHashes = true;
        bool disablePersona = false;
        bool disableCustomSkins = false;
        std::string chatRestriction = "None";
        bool packetRateLimiter = true;
        bool texturepackRequired = false;
        bool networkEncryption = true;
        bool encryptLocal = false;
        std::string worldStorage = "leveldb";
        uint32_t autosaveSeconds = 300;
        bool downloadVanillaPack = true;
        std::vector<std::pair<std::string, std::string>> extraProperties;

        static ServerConfig LoadDirectory(const std::string& dir, std::string* error = nullptr);
        static ServerConfig LoadProperties(const std::string& path, std::string* error = nullptr);
        bool SaveProperties(const std::string& path) const;
        // The server.properties text for this configuration (what SaveProperties writes).
        std::string PropertiesText() const;
        // The settings a brand-new server starts with; the shipped server.properties is these.
        static ServerConfig FirstRun();
        bool SetProperty(const std::string& key, const std::string& value, std::string* warning = nullptr);
        static uint64_t SeedFromText(const std::string& text);

        static ServerConfig Load(const std::string& path, std::string* error = nullptr);
        bool Save(const std::string& path) const;
    };
}
