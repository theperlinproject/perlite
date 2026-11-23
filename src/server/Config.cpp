#include "server/Config.hpp"

#include "common/Json.hpp"
#include "common/Log.hpp"
#include "common/Strings.hpp"
#include "common/JsonText.hpp"
#include "common/FileIo.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <fstream>

namespace perlite {

    ServerConfig ServerConfig::Load(const std::string& path, std::string* error) {
        if (path.size() > 11 && path.compare(path.size() - 11, 11, ".properties") == 0)
            return LoadProperties(path, error);
        ServerConfig c;
        std::string contents;
        if (!file::Read(path, contents))
            return c;
        json::Value root;
        std::string err;
        if (!json::Parse(contents, root, &err)) {
            if (error)
                *error = path + ": " + err;
            return c;
        }
        c.name = root["name"].String(c.name);
        c.motd = root["motd"].String(c.motd);
        c.levelName = root["level_name"].String(c.levelName);
        c.xboxAuth = root["xbox_auth"].Bool(c.xboxAuth);
        for (const json::Value& k : root["auth_root_keys"].items)
            if (k.IsString())
                c.authRootKeys.push_back(k.text);
        c.pluginDir = root["plugin_dir"].String(c.pluginDir);
        c.blockDataDir = root["block_data_dir"].String(c.blockDataDir);
        c.maxPlayers =
            static_cast<uint32_t>(std::clamp<int64_t>(root["max_players"].Int(c.maxPlayers), 1, 1000));
        c.viewDistance =
            static_cast<uint32_t>(std::clamp<int64_t>(root["view_distance"].Int(c.viewDistance), 2, 64));
        c.tickRate = static_cast<uint32_t>(std::clamp<int64_t>(root["tick_rate"].Int(c.tickRate), 1, 200));
        c.gameMode = static_cast<int>(root["gamemode"].Int(c.gameMode));
        if (c.gameMode != 0 && c.gameMode != 1 && c.gameMode != 2 && c.gameMode != 6)
            c.gameMode = 0;
        c.seed = static_cast<uint64_t>(root["seed"].Int(static_cast<int64_t>(c.seed)));
        c.whitelist = root["whitelist"].Bool(c.whitelist);
        c.lanDiscovery = root["lan_discovery"].Bool(c.lanDiscovery);
        c.maxThreads = static_cast<decltype(c.maxThreads)>(
            std::clamp<int64_t>(root["max_threads"].Int(static_cast<int64_t>(c.maxThreads)), 0, 256));
        c.nethernet = root["nethernet"].Bool(c.nethernet);
        c.port = static_cast<uint16_t>(std::clamp<int64_t>(root["port"].Int(c.port), 1, 65535));
        c.bindAddress = root["bind_address"].String(c.bindAddress);
        for (const json::Value& s : root["ice_servers"].items)
            if (s.IsString())
                c.iceServers.push_back(s.text);
        for (const json::Value& o : root["operators"].items)
            if (o.IsString())
                c.operators.push_back(o.text);
        c.iceUdpMux = root["ice_udp_mux"].Bool(c.iceUdpMux);
        c.iceBindAddress = root["ice_bind_address"].String(c.iceBindAddress);
        c.generator = root["generator"].String(c.generator);
        c.terrainBackend = root["terrain_backend"].String(c.terrainBackend);
        if (root["flat_layers"].IsArray()) {
            c.flatLayers.clear();
            for (const json::Value& l : root["flat_layers"].items) {
                FlatLayer layer;
                layer.block = l["block"].String();
                layer.count = static_cast<int>(std::clamp<int64_t>(l["count"].Int(1), 1, 384));
                if (!layer.block.empty())
                    c.flatLayers.push_back(std::move(layer));
            }
        }
        return c;
    }

    bool ServerConfig::Save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << "{\n"
            << "  \"name\": \"" << ::perlite::json::Escape(name) << "\",\n"
            << "  \"motd\": \"" << ::perlite::json::Escape(motd) << "\",\n"
            << "  \"level_name\": \"" << ::perlite::json::Escape(levelName) << "\",\n"
            << "  \"max_players\": " << maxPlayers << ",\n"
            << "  \"view_distance\": " << viewDistance << ",\n"
            << "  \"tick_rate\": " << tickRate << ",\n"
            << "  \"gamemode\": " << gameMode << ",\n"
            << "  \"seed\": " << static_cast<int64_t>(seed) << ",\n"
            << "  \"nethernet\": " << (nethernet ? "true" : "false") << ",\n"
            << "  \"port\": " << port << ",\n"
            << "  \"bind_address\": \"" << ::perlite::json::Escape(bindAddress) << "\",\n"
            << "  \"ice_udp_mux\": " << (iceUdpMux ? "true" : "false") << ",\n"
            << "  \"ice_bind_address\": \"" << ::perlite::json::Escape(iceBindAddress) << "\",\n"
            << "  \"generator\": \"" << ::perlite::json::Escape(generator) << "\",\n"
            << "  \"terrain_backend\": \"" << ::perlite::json::Escape(terrainBackend) << "\",\n"
            << "  \"operators\": [";
        for (size_t i = 0; i < operators.size(); i++)
            out << (i ? ", " : "") << "\"" << ::perlite::json::Escape(operators[i]) << "\"";
        out << "],\n"
            << "  \"xbox_auth\": " << (xboxAuth ? "true" : "false") << ",\n"
            << "  \"whitelist\": " << (whitelist ? "true" : "false") << ",\n"
            << "  \"lan_discovery\": " << (lanDiscovery ? "true" : "false") << ",\n"
            << "  \"plugin_dir\": \"" << ::perlite::json::Escape(pluginDir) << "\",\n"
            << "  \"block_data_dir\": \"" << ::perlite::json::Escape(blockDataDir) << "\",\n"
            << "  \"flat_layers\": [";
        for (size_t i = 0; i < flatLayers.size(); i++)
            out << (i ? ", " : "") << "{\"block\": \"" << ::perlite::json::Escape(flatLayers[i].block)
                << "\", \"count\": " << flatLayers[i].count << "}";
        out << "]\n}\n";
        return out.good();
    }

    namespace {

        bool ParseBool(const std::string& v, bool& out) {
            const std::string t = str::Lower(v);
            if (t == "true" || t == "1") {
                out = true;
                return true;
            }
            if (t == "false" || t == "0") {
                out = false;
                return true;
            }
            return false;
        }

        bool ParseInt(const std::string& v, int64_t& out) {
            if (v.empty())
                return false;
            char* end = nullptr;
            const long long n = std::strtoll(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0')
                return false;
            out = n;
            return true;
        }

        std::vector<std::string> Split(const std::string& s, char sep) {
            std::vector<std::string> out;
            size_t start = 0;
            for (;;) {
                const size_t at = s.find(sep, start);
                std::string part =
                    str::Trim(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
                if (!part.empty())
                    out.push_back(std::move(part));
                if (at == std::string::npos)
                    break;
                start = at + 1;
            }
            return out;
        }

        bool ParsePortRange(const std::string& text, uint16_t& first, uint16_t& last) {
            const size_t dash = text.find('-');
            int64_t a = 0, b = 0;
            if (dash == std::string::npos) {
                if (!ParseInt(str::Trim(text), a))
                    return false;
                b = a;
            } else if (!ParseInt(str::Trim(text.substr(0, dash)), a) ||
                       !ParseInt(str::Trim(text.substr(dash + 1)), b)) {
                return false;
            }
            if (a < 1 || b > 65535 || a > b)
                return false;
            first = static_cast<uint16_t>(a);
            last = static_cast<uint16_t>(b);
            return true;
        }

        bool ParseUdpPorts(const std::string& text, ServerConfig& c) {
            std::string t = str::Trim(text);
            c.udpPortsSet = true;
            c.udpPublicAddress.clear();
            c.udpPublicBegin = c.udpPublicEnd = 0;
            if (t.empty()) {
                c.udpPortBegin = c.udpPortEnd = 0;
                return true;
            }
            if (t[0] == '[') {
                const size_t close = t.find(']');
                if (close == std::string::npos)
                    return false;
                c.udpPublicAddress = t.substr(1, close - 1);
                t = t.substr(close + 1);
                if (t.empty() || t[0] != ':')
                    return false;
                t.erase(0, 1);
            } else if (std::ranges::count(t, ':') == 2) {
                const size_t colon = t.find(':');
                c.udpPublicAddress = t.substr(0, colon);
                t.erase(0, colon + 1);
            }
            const std::vector<std::string> parts = Split(t, ':');
            if (parts.empty() || parts.size() > 2)
                return false;
            uint16_t localFirst = 0, localLast = 0, pubFirst = 0, pubLast = 0;
            if (!ParsePortRange(parts.back(), localFirst, localLast))
                return false;
            if (parts.size() == 2) {
                if (!ParsePortRange(parts[0], pubFirst, pubLast) ||
                    pubLast - pubFirst != localLast - localFirst)
                    return false;
                c.udpPublicBegin = pubFirst;
                c.udpPublicEnd = pubLast;
            } else if (!c.udpPublicAddress.empty()) {
                return false;
            }
            c.udpPortBegin = localFirst;
            c.udpPortEnd = localLast;
            return true;
        }

        std::string UdpPortsText(const ServerConfig& c) {
            if (!c.udpPortsSet)
                return std::to_string(c.port);
            const auto range = [](uint16_t a, uint16_t b) {
                return a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b);
            };
            if (c.udpPortBegin == 0)
                return "";
            std::string out = range(c.udpPortBegin, c.udpPortEnd);
            if (c.udpPublicBegin != 0)
                out = range(c.udpPublicBegin, c.udpPublicEnd) + ":" + out;
            if (!c.udpPublicAddress.empty())
                out =
                    (c.udpPublicAddress.contains(':') ? "[" + c.udpPublicAddress + "]" : c.udpPublicAddress) +
                    ":" + out;
            return out;
        }

        const char* GameModeName(int mode) {
            return mode == 1 ? "creative" : mode == 2 ? "adventure" : mode == 6 ? "spectator" : "survival";
        }
        const char* DifficultyName(int d) {
            return d == 0 ? "peaceful" : d == 2 ? "normal" : d == 3 ? "hard" : "easy";
        }

        // Every setting written to server.properties, in order, with the comment written above it.
        // Keys a file has that are not listed here are read and written back unchanged.
        struct PropertyKey {
            const char* key;
            const char* fallback;
            const char* comment;
        };
        constexpr PropertyKey kPropertyKeys[] = {
            {"server-name", "Perlite Server",
             "Name shown in the server list and the LAN list. Any text without ';'."},
            {"gamemode", "survival", "Game mode for new players: survival, creative or adventure."},
            {"force-gamemode", "false",
             "true: players get gamemode above on every join. false: each player keeps their own game mode."},
            {"difficulty", "easy", "World difficulty: peaceful, easy, normal or hard."},
            {"allow-cheats", "false", "true lets players with permission run commands like /give and /tp."},
            {"max-players", "20", "Most players online at once."},
            {"online-mode", "false",
             "true: every player must be signed in to Xbox Live. Turn it on for servers open to the internet."},
            {"allow-list", "false", "true: only players listed in allowlist.json can join (manage it with /allowlist)."},
            {"server-ip", "", "Address to listen on. Empty listens on every network interface."},
            {"server-port", "19132",
             "Port players join on. Open it for TCP (the join request) and UDP (gameplay) in your firewall."},
            {"server-udp-ports", "19132",
             "UDP port(s) for gameplay traffic: a port, or a first-last range, or public:local."},
            {"enable-lan-visibility", "true",
             "Show the server under Friends > LAN Games (UDP 7551 on the local network)."},
            {"view-distance", "12", "Farthest chunk distance sent to players."},
            {"tick-distance", "4", "Chunk distance around players where blocks and mobs are ticked (4 to 12)."},
            {"player-idle-timeout", "30", "Minutes before an idle player is kicked. 0 never kicks."},
            {"max-threads", "8", "Threads for world generation and saving. 0 uses every core."},
            {"level-name", "world", "Name of the world folder under worlds/."},
            {"level-seed", "", "Seed for a new world: a number or any text. Empty picks a random seed."},
            {"default-player-permission-level", "member",
             "Permission for players not listed in permissions.json: visitor, member or operator."},
            {"texturepack-required", "false", "true: players must accept the server's resource packs to join."},
            {"chat-restriction", "None",
             "None, Dropped (chat is hidden) or Disabled (the chat button is off)."},
            {"compression-threshold", "1", "Smallest packet size in bytes that is compressed (0 to 65535)."},
            {"compression-algorithm", "zlib", "Packet compression: zlib or snappy."},
            {"client-side-chunk-generation-enabled", "true",
             "true lets clients generate far-away terrain themselves to see beyond view-distance."},
            {"block-network-ids-are-hashes", "true", "Send block ids as hashes, as current clients expect."},
            {"disable-persona", "false", "true: persona (character creator) skins are not shown to other players."},
            {"disable-custom-skins", "false", "true: custom skins are replaced with the default skins."},
            {"enable-packet-rate-limiter", "true", "Disconnect players who flood the server with packets."},
            {"network-encryption", "true",
             "Encrypt connections. Over NetherNet the handshake runs but DTLS already encrypts the stream."},
            {"debug-log-packets", "false", "Log every packet sent and received while a player is joining."},
            {"perlite-world-storage", "leveldb",
             "leveldb saves the world in worlds/<level-name>; none keeps it in memory only."},
            {"perlite-autosave-seconds", "300", "Seconds between automatic saves. 0 saves only on stop and /save."},
            {"perlite-download-vanilla-pack", "true",
             "Download the latest Bedrock Dedicated Server from minecraft.net on first start and keep only its vanilla "
             "behavior packs (mobs, block shapes, recipes, structures, loot); the Minecraft EULA applies: "
             "https://www.minecraft.net/eula."},
            {"perlite-generator", "vanilla", "Terrain for new chunks: vanilla or flat."},
            {"perlite-flat-layers", "minecraft:bedrock*1,minecraft:dirt*2,minecraft:grass_block*1",
             "Layers of a flat world, bottom up: block*count separated by commas."},
            {"perlite-plugin-dir", "", "Folder with plugins. Empty uses plugins/ in the data folder."},
            {"perlite-block-data-dir", "", "Folder with block data. Empty uses the data folder beside the program."},
            {"perlite-download-structures", "true",
             "Also download the vanilla behavior pack (see perlite-download-vanilla-pack) for its structure templates and "
             "loot tables; either setting turns the one download on."},
            {"perlite-structure-pack", "",
             "A Bedrock vanilla behavior pack (from your own Bedrock Dedicated Server or game install) whose structures "
             "folder holds the village, outpost, bastion and other templates. Empty looks for behavior_packs/vanilla "
             "beside the server, then for the downloaded copy. Without it only structures built in code generate."},
            {"perlite-ice-servers", "", "STUN/TURN servers for NetherNet, comma separated. Usually left empty."},
            {"perlite-ice-bind-address", "", "Address NetherNet gameplay traffic is bound to. Empty uses all."},
            {"perlite-auth-root-keys", "", "Extra trusted Xbox Live root keys, comma separated. Usually left empty."},
        };

        // Accepted when reading older files, but not written back: they do nothing here.
        constexpr const char* kRetiredKeys[] = {
            "transport", "server-portv6", "persistent-ticking-area-max-count", "persistent-ticking-area-max-chunks",
            "transient-ticking-area-max-chunks", "content-log-file-enabled", "content-log-console-output-enabled",
            "content-log-level", "server-authoritative-movement-strict", "server-authoritative-dismount-strict",
            "server-authoritative-entity-interactions-strict", "server-authoritative-entity-attack-request-validation",
            "server-authoritative-entity-attack-range-validation", "server-authoritative-entity-attack-range-tolerance",
            "player-position-acceptance-threshold", "player-movement-action-direction-threshold",
            "server-authoritative-block-breaking-pick-range-scalar", "disable-player-interaction",
            "server-build-radius-ratio", "allow-outbound-script-debugging", "allow-inbound-script-debugging",
            "script-debugger-auto-attach", "emit-server-telemetry", "debug-skip-packets",
        };

        std::string JoinList(const std::vector<std::string>& v) {
            std::string out;
            for (size_t i = 0; i < v.size(); i++)
                out += (i ? "," : "") + v[i];
            return out;
        }

        std::string FlatLayersText(const std::vector<FlatLayer>& layers) {
            std::string out;
            for (size_t i = 0; i < layers.size(); i++)
                out += (i ? "," : "") + layers[i].block + "*" + std::to_string(layers[i].count);
            return out;
        }

        const char* Bool(bool b) {
            return b ? "true" : "false";
        }
    }

    uint64_t ServerConfig::SeedFromText(const std::string& text) {
        const std::string t = str::Trim(text);
        int64_t n = 0;
        if (ParseInt(t, n))
            return static_cast<uint64_t>(n);
        int32_t h = 0;
        const auto feed = [&h](uint32_t unit) {
            h = static_cast<int32_t>(static_cast<uint32_t>(h) * 31u + unit);
        };
        for (size_t i = 0; i < t.size();) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            uint32_t cp = c;
            size_t len = 1;
            if (c >= 0xF0 && i + 3 < t.size()) {
                cp = c & 0x07;
                len = 4;
            } else if (c >= 0xE0 && i + 2 < t.size()) {
                cp = c & 0x0F;
                len = 3;
            } else if (c >= 0xC0 && i + 1 < t.size()) {
                cp = c & 0x1F;
                len = 2;
            }
            for (size_t k = 1; k < len; k++)
                cp = (cp << 6) | (static_cast<unsigned char>(t[i + k]) & 0x3F);
            i += len;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                feed(0xD800 + (cp >> 10));
                feed(0xDC00 + (cp & 0x3FF));
            } else {
                feed(cp);
            }
        }
        return static_cast<uint64_t>(static_cast<int64_t>(h));
    }

    bool ServerConfig::SetProperty(const std::string& rawKey, const std::string& rawValue,
                                   std::string* warning) {
        const std::string key = rawKey.starts_with("perlin-") ? "perlite-" + rawKey.substr(7) : rawKey;
        const std::string v = str::Trim(rawValue);
        const std::string lower = str::Lower(v);
        bool b = false;
        int64_t n = 0;
        const auto bad = [&](const char* expected) {
            if (warning)
                *warning = key + "=" + v + ": expected " + expected + ", keeping the current value";
            return true;
        };
        const auto boolean = [&](bool& field) {
            if (ParseBool(v, b))
                field = b;
            else
                bad("true or false");
            return true;
        };
        const auto number = [&](auto& field, int64_t lo, int64_t hi) {
            if (ParseInt(v, n))
                field = static_cast<std::remove_reference_t<decltype(field)>>(std::clamp(n, lo, hi));
            else
                bad("a whole number");
            return true;
        };
        if (key == "server-name") {
            name = v;
            motd = v;
            return true;
        }
        if (key == "gamemode") {
            if (lower == "survival" || lower == "0")
                gameMode = 0;
            else if (lower == "creative" || lower == "1")
                gameMode = 1;
            else if (lower == "adventure" || lower == "2")
                gameMode = 2;
            else if (lower == "spectator" || lower == "6")
                gameMode = 6;
            else
                bad("survival, creative, adventure or spectator");
            return true;
        }
        if (key == "force-gamemode")
            return boolean(forceGamemode);
        if (key == "difficulty") {
            if (lower == "peaceful" || lower == "0")
                difficulty = 0;
            else if (lower == "easy" || lower == "1")
                difficulty = 1;
            else if (lower == "normal" || lower == "2")
                difficulty = 2;
            else if (lower == "hard" || lower == "3")
                difficulty = 3;
            else
                bad("peaceful, easy, normal or hard");
            return true;
        }
        if (key == "allow-cheats")
            return boolean(allowCheats);
        if (key == "max-players")
            return number(maxPlayers, 1, 1000);
        if (key == "online-mode")
            return boolean(xboxAuth);
        if (key == "allow-list")
            return boolean(whitelist);
        if (key == "server-port")
            return number(port, 1, 65535);
        if (key == "server-portv6")
            return number(portV6, 1, 65535);
        if (key == "server-ip") {
            bindAddress = v.empty() ? "0.0.0.0" : v;
            return true;
        }
        if (key == "server-udp-ports") {
            ServerConfig trial = *this;
            if (ParseUdpPorts(v, trial))
                *this = std::move(trial);
            else
                bad("a port, a first-last range, or public:local");
            return true;
        }
        if (key == "transport") {
            if (lower == "nethernet" || lower == "nethernet-only")
                transport = "nethernet";
            else if (lower == "raknet" || lower == "both") {
                // RakNet is gone: Bedrock 26.60+ clients only join over NetherNet.
                transport = "nethernet";
                if (warning)
                    *warning = key + "=" + v + ": RakNet support was removed, using nethernet";
                return true;
            }
            else
                bad("nethernet");
            return true;
        }
        if (key == "enable-lan-visibility")
            return boolean(lanDiscovery);
        if (key == "view-distance")
            return number(viewDistance, 2, 64);
        if (key == "tick-distance")
            return number(tickDistance, 4, 12);
        if (key == "debug-log-packets")
            return boolean(debugLogPackets);
        if (key == "debug-skip-packets") {
            debugSkipPackets.clear();
            size_t start = 0;
            while (start <= v.size()) {
                const size_t comma = std::min(v.find(',', start), v.size());
                const std::string part = v.substr(start, comma - start);
                if (part.find_first_not_of(" ") != std::string::npos && part.find_first_not_of("0123456789 ") == std::string::npos)
                    debugSkipPackets.push_back(static_cast<uint32_t>(std::stoul(part)));
                start = comma + 1;
            }
            return true;
        }
        if (key == "player-idle-timeout")
            return number(idleTimeoutMinutes, 0, 1000000);
        if (key == "max-threads")
            return number(maxThreads, 0, 256);
        if (key == "level-name") {
            levelName = v.empty() ? "Bedrock level" : v;
            return true;
        }
        if (key == "level-seed") {
            levelSeedText = v;
            randomSeed = v.empty();
            if (!randomSeed)
                seed = SeedFromText(v);
            return true;
        }
        if (key == "default-player-permission-level") {
            if (lower == "visitor" || lower == "member" || lower == "operator")
                defaultPermission = lower;
            else
                bad("visitor, member or operator");
            return true;
        }
        if (key == "compression-threshold")
            return number(compressionThreshold, 0, 65535);
        if (key == "compression-algorithm") {
            if (lower == "zlib" || lower == "snappy")
                compressionAlgorithm = lower;
            else
                bad("zlib or snappy");
            return true;
        }
        if (key == "client-side-chunk-generation-enabled")
            return boolean(clientSideChunkGeneration);
        if (key == "block-network-ids-are-hashes")
            return boolean(blockNetworkIdsAreHashes);
        if (key == "disable-persona")
            return boolean(disablePersona);
        if (key == "disable-custom-skins")
            return boolean(disableCustomSkins);
        if (key == "enable-packet-rate-limiter")
            return boolean(packetRateLimiter);
        if (key == "texturepack-required")
            return boolean(texturepackRequired);
        if (key == "network-encryption")
            return boolean(networkEncryption);
        if (key == "perlite-world-storage") {
            if (lower == "leveldb" || lower == "none")
                worldStorage = lower;
            else
                bad("leveldb or none");
            return true;
        }
        if (key == "perlite-autosave-seconds")
            return number(autosaveSeconds, 0, 86400);
        if (key == "perlite-download-vanilla-pack")
            return boolean(downloadVanillaPack);
        if (key == "chat-restriction") {
            if (lower == "none")
                chatRestriction = "None";
            else if (lower == "dropped")
                chatRestriction = "Dropped";
            else if (lower == "disabled")
                chatRestriction = "Disabled";
            else
                bad("None, Dropped or Disabled");
            return true;
        }
        if (key == "perlite-generator") {
            generator = lower;
            return true;
        }
        if (key == "perlite-terrain-backend")
            return true; // gone (terrain is always generated on the cpu); old files keep working
        if (key == "perlite-plugin-dir") {
            pluginDir = v;
            return true;
        }
        if (key == "perlite-block-data-dir") {
            blockDataDir = v;
            return true;
        }
        if (key == "perlite-download-structures") {
            downloadStructures = lower == "true";
            return true;
        }
        if (key == "perlite-structure-pack") {
            structurePack = v;
            return true;
        }
        if (key == "perlite-ice-servers") {
            iceServers = Split(v, ',');
            return true;
        }
        if (key == "perlite-ice-bind-address") {
            iceBindAddress = v;
            return true;
        }
        if (key == "perlite-auth-root-keys") {
            authRootKeys = Split(v, ',');
            return true;
        }
        if (key == "perlite-flat-layers") {
            std::vector<FlatLayer> parsed;
            for (const std::string& item : Split(v, ',')) {
                FlatLayer layer;
                const size_t star = item.find('*');
                layer.block = str::Trim(item.substr(0, star));
                int64_t count = 1;
                if (star != std::string::npos && !ParseInt(str::Trim(item.substr(star + 1)), count))
                    count = 1;
                layer.count = static_cast<int>(std::clamp<int64_t>(count, 1, 384));
                if (!layer.block.empty())
                    parsed.push_back(std::move(layer));
            }
            if (!parsed.empty())
                flatLayers = std::move(parsed);
            return true;
        }
        for (auto& [k, val] : extraProperties)
            if (k == key) {
                val = v;
                return false;
            }
        extraProperties.emplace_back(key, v);
        return false;
    }

    ServerConfig ServerConfig::LoadProperties(const std::string& path, std::string* error) {
        ServerConfig c;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error)
                *error = path + ": cannot open";
            return c;
        }
        c.randomSeed = true;
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
                line.erase(0, 3);
            first = false;
            line = str::Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                LogWarn(path + ": ignored line without '=': " + line);
                continue;
            }
            std::string warning;
            c.SetProperty(str::Trim(line.substr(0, eq)), line.substr(eq + 1), &warning);
            if (!warning.empty())
                LogWarn(path + ": " + warning);
        }
        return c;
    }

    ServerConfig ServerConfig::LoadDirectory(const std::string& dir, std::string* error) {
        const std::filesystem::path props = std::filesystem::path(dir) / "server.properties";
        const std::filesystem::path legacy = std::filesystem::path(dir) / "server.json";
        std::error_code ec;
        if (std::filesystem::exists(props, ec))
            return LoadProperties(props.string(), error);
        if (std::filesystem::exists(legacy, ec))
            return Load(legacy.string(), error);
        ServerConfig fresh;
        fresh.randomSeed = true;
        return fresh;
    }

    bool ServerConfig::SaveProperties(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << PropertiesText();
        return out.good();
    }

    ServerConfig ServerConfig::FirstRun() {
        ServerConfig c;
        c.randomSeed = true;
        c.udpPortsSet = true;
        c.udpPortBegin = c.udpPortEnd = c.port;
        c.downloadStructures = true;
        return c;
    }

    std::string ServerConfig::PropertiesText() const {
        const auto current = [&](const std::string& key, std::string& value) {
            if (key == "server-name")
                value = name;
            else if (key == "gamemode")
                value = GameModeName(gameMode);
            else if (key == "force-gamemode")
                value = Bool(forceGamemode);
            else if (key == "difficulty")
                value = DifficultyName(difficulty);
            else if (key == "allow-cheats")
                value = Bool(allowCheats);
            else if (key == "max-players")
                value = std::to_string(maxPlayers);
            else if (key == "online-mode")
                value = Bool(xboxAuth);
            else if (key == "allow-list")
                value = Bool(whitelist);
            else if (key == "server-port")
                value = std::to_string(port);
            else if (key == "server-ip")
                value = bindAddress == "0.0.0.0" ? "" : bindAddress;
            else if (key == "server-udp-ports")
                value = udpPortsSet ? UdpPortsText(*this) : std::to_string(port);
            else if (key == "enable-lan-visibility")
                value = Bool(lanDiscovery);
            else if (key == "view-distance")
                value = std::to_string(viewDistance);
            else if (key == "tick-distance")
                value = std::to_string(tickDistance);
            else if (key == "player-idle-timeout")
                value = std::to_string(idleTimeoutMinutes);
            else if (key == "max-threads")
                value = std::to_string(maxThreads);
            else if (key == "level-name")
                value = levelName;
            else if (key == "level-seed")
                value = randomSeed ? std::string()
                                   : (levelSeedText.empty() ? (seed ? std::to_string(static_cast<int64_t>(seed))
                                                                    : std::string())
                                                            : levelSeedText);
            else if (key == "default-player-permission-level")
                value = defaultPermission;
            else if (key == "compression-threshold")
                value = std::to_string(compressionThreshold);
            else if (key == "compression-algorithm")
                value = compressionAlgorithm;
            else if (key == "client-side-chunk-generation-enabled")
                value = Bool(clientSideChunkGeneration);
            else if (key == "block-network-ids-are-hashes")
                value = Bool(blockNetworkIdsAreHashes);
            else if (key == "disable-persona")
                value = Bool(disablePersona);
            else if (key == "disable-custom-skins")
                value = Bool(disableCustomSkins);
            else if (key == "enable-packet-rate-limiter")
                value = Bool(packetRateLimiter);
            else if (key == "texturepack-required")
                value = Bool(texturepackRequired);
            else if (key == "chat-restriction")
                value = chatRestriction;
            else if (key == "network-encryption")
                value = Bool(networkEncryption);
            else if (key == "debug-log-packets")
                value = Bool(debugLogPackets);
            else if (key == "perlite-world-storage")
                value = worldStorage;
            else if (key == "perlite-autosave-seconds")
                value = std::to_string(autosaveSeconds);
            else if (key == "perlite-download-vanilla-pack")
                value = Bool(downloadVanillaPack);
            else if (key == "perlite-generator")
                value = generator;
            else if (key == "perlite-flat-layers")
                value = FlatLayersText(flatLayers);
            else if (key == "perlite-plugin-dir")
                value = pluginDir;
            else if (key == "perlite-block-data-dir")
                value = blockDataDir;
            else if (key == "perlite-download-structures")
                value = Bool(downloadStructures);
            else if (key == "perlite-structure-pack")
                value = structurePack;
            else if (key == "perlite-ice-servers")
                value = JoinList(iceServers);
            else if (key == "perlite-ice-bind-address")
                value = iceBindAddress;
            else if (key == "perlite-auth-root-keys")
                value = JoinList(authRootKeys);
            else
                return false;
            return true;
        };
        std::ostringstream out;
        out << "# Perlite server settings. Lines starting with # are comments; edit a value, then restart.\n";
        std::vector<std::string> written;
        for (const PropertyKey& k : kPropertyKeys) {
            std::string value = k.fallback;
            if (!current(k.key, value))
                for (const auto& [ek, ev] : extraProperties)
                    if (ek == k.key)
                        value = ev;
            out << "# " << k.comment << "\n" << k.key << "=" << value << "\n";
            written.push_back(k.key);
        }
        bool header = false;
        for (const auto& [ek, ev] : extraProperties) {
            if (std::ranges::find(written, ek) != written.end() ||
                std::ranges::find(kRetiredKeys, ek, [](const char* r) { return std::string(r); }) !=
                    std::end(kRetiredKeys))
                continue;
            if (!header) {
                out << "# Other settings (kept from this file).\n";
                header = true;
            }
            out << ek << "=" << ev << "\n";
            written.push_back(ek);
        }
        return out.str();
    }
}
