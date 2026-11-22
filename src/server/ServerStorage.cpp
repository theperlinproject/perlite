#include "server/Server.hpp"

#include <fstream>

#include "common/Log.hpp"
#include "world/level/storage/WorldStorage.hpp"

#include <chrono>
#include <filesystem>

namespace perlite {
    namespace fs = std::filesystem;

    bool Server::OpenWorld(std::string& error) {
        if (config.worldStorage != "leveldb" || config.dataDir.empty())
            return true;
        if (!LevelDb::Available()) {
            LogWarn("This build cannot save worlds (no LevelDB); the world is kept in memory only");
            return true;
        }
        std::string name = config.levelName.empty() ? std::string("Bedrock level") : config.levelName;
        for (char& c : name)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|')
                c = '_';
        const fs::path dir = fs::path(config.dataDir) / "worlds" / name;
        auto world = std::make_unique<WorldStorage>();
        std::string why;
        if (!world->Open(dir.string(), why)) {
            error = "cannot open the world at " + dir.string() + ": " + why;
            return false;
        }
        LevelInfo info;
        info.name = config.levelName;
        if (world->LoadLevel(info)) {
            config.seed = info.seed;
            config.randomSeed = false;
            spawnX = info.spawnX;
            spawnY = info.spawnY;
            spawnZ = info.spawnZ;
            dayTime = info.time;
            tick = static_cast<uint64_t>(std::max<int64_t>(0, info.currentTick));
            raining = info.raining;
            thundering = info.thundering;
            for (const auto& [rule, value] : info.boolRules)
                if (auto it = gameRules.find(rule); it != gameRules.end() && it->second.boolean)
                    it->second.value = value ? 1 : 0;
            for (const auto& [rule, value] : info.intRules)
                if (auto it = gameRules.find(rule); it != gameRules.end() && !it->second.boolean)
                    it->second.value = value;
            levelLoaded = true;
            LogInfo("Loaded world '" + info.name + "' from " + dir.string() + " (seed " +
                    std::to_string(static_cast<int64_t>(config.seed)) + ")");
        } else {
            LogInfo("Creating a new world in " + dir.string() + " (seed " +
                    std::to_string(static_cast<int64_t>(config.seed)) + ")");
        }
        storage = std::move(world);
        LoadPortals();
        lastSave = std::chrono::steady_clock::now();
        return true;
    }

    // Nether portals are remembered in portals.txt in the world folder, one per line:
    // dimension x y z axis width height. Travel after a restart then finds the portal it made.
    void Server::LoadPortals() {
        if (!storage)
            return;
        std::ifstream in(fs::path(storage->Directory()) / "portals.txt");
        PortalFrame f;
        while (in >> f.dimension >> f.x >> f.y >> f.z >> f.axis >> f.width >> f.height)
            if (f.dimension >= 0 && f.dimension < kDimensionCount && f.width > 0 && f.height > 0)
                portals.push_back(f);
    }

    void Server::SavePortals() {
        if (!storage)
            return;
        const fs::path path = fs::path(storage->Directory()) / "portals.txt";
        std::ofstream out(path.string() + ".tmp", std::ios::trunc);
        for (const PortalFrame& f : portals)
            out << f.dimension << ' ' << f.x << ' ' << f.y << ' ' << f.z << ' ' << f.axis << ' ' << f.width << ' '
                << f.height << '\n';
        out.close();
        std::error_code ec;
        fs::rename(path.string() + ".tmp", path, ec);
    }

    LevelInfo Server::CurrentLevelInfo() const {
        LevelInfo info;
        info.name = config.levelName;
        info.seed = config.seed;
        info.spawnX = spawnX;
        info.spawnY = spawnY;
        info.spawnZ = spawnZ;
        info.time = dayTime;
        info.currentTick = static_cast<int64_t>(tick);
        info.gameType = config.gameMode;
        info.difficulty = config.difficulty;
        info.generator = config.generator == "flat" ? 2 : 1;
        info.raining = raining;
        info.thundering = thundering;
        for (const auto& [rule, value] : gameRules) {
            if (value.boolean)
                info.boolRules.emplace_back(rule, value.value != 0);
            else
                info.intRules.emplace_back(rule, value.value);
        }
        return info;
    }

    void Server::SaveWorld(bool announce) {
        if (!storage)
            return;
        const auto began = std::chrono::steady_clock::now();
        if (announce) {
            LogInfo("Saving players");
            LogInfo("Saving chunks for level '" + config.levelName + "'/minecraft:overworld");
        }
        for (Session* s : InGamePlayers())
            SavePlayer(*s);
        size_t chunksSaved = chunks ? chunks->SaveDirty() : 0;
        for (auto& w : otherWorlds)
            if (w)
                chunksSaved += w->SaveDirty();
        SaveContainers();
        SavePortals();
        if (announce)
            storage->DrainSaves();
        storage->SaveLevel(CurrentLevelInfo());
        std::string error;
        if (!storage->Flush(&error) && !error.empty())
            LogError("Saving the world failed: " + error);
        lastSave = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(lastSave - began).count();
        const std::string summary =
            "All chunks are saved (" + std::to_string(chunksSaved) + " chunks, " + std::to_string(ms) + " ms)";
        if (announce)
            LogInfo(summary);
        else if (chunksSaved > 0)
            LogDebug("Autosave: " + summary);
    }

    void Server::AutosaveTick() {
        if (!storage || config.autosaveSeconds == 0 || savingHeld)
            return;
        if (std::chrono::steady_clock::now() - lastSave >= std::chrono::seconds(config.autosaveSeconds))
            SaveWorld();
    }

    void Server::LoadPlayer(Session& session) {
        if (!storage)
            return;
        nbt::Value data;
        if (storage->LoadPlayer(session.StorageId(), data))
            session.LoadState(data);
    }

    void Server::SavePlayer(Session& session) {
        if (!storage || session.Name().empty())
            return;
        storage->SavePlayer(session.StorageId(), session.SaveState());
    }

    void Server::CloseWorld() {
        if (!storage)
            return;
        LogInfo("Saving worlds");
        SaveWorld(true);
        if (chunks)
            chunks->SetStore(nullptr);
        for (auto& w : otherWorlds)
            if (w)
                w->SetStore(nullptr);
        storage->Close();
        storage.reset();
    }
}
