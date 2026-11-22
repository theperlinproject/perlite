#pragma once
#include "protocol/NbtTree.hpp"
#include "world/level/chunk/Chunk.hpp"
#include "world/level/storage/LevelDb.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <string>
#include <vector>

namespace perlite {

    class ChunkStore {
    public:
        virtual ~ChunkStore() = default;
        virtual bool LoadChunk(int dimension, ChunkPos pos, ChunkColumn& out) = 0;
        virtual void SaveChunk(int dimension, ChunkPos pos, const ChunkColumn& column) = 0;
        // Saves a snapshot of the column without blocking the caller; by default it saves in place.
        virtual void QueueChunk(int dimension, ChunkPos pos, std::shared_ptr<const ChunkColumn> column) {
            SaveChunk(dimension, pos, *column);
        }
    };

    struct LevelInfo {
        std::string name;
        uint64_t seed = 0;
        int spawnX = 0, spawnY = 64, spawnZ = 0;
        int64_t time = 0;
        int64_t currentTick = 0;
        int gameType = 0;
        int difficulty = 1;
        int generator = 1;
        bool raining = false;
        bool thundering = false;
        std::vector<std::pair<std::string, int>> intRules;
        std::vector<std::pair<std::string, bool>> boolRules;
    };

    class WorldStorage final : public ChunkStore {
    public:
        static constexpr int kChunkVersion = 40;
        static constexpr int32_t kBlockStateVersion = (1 << 24) | (21 << 16) | (60 << 8) | 33;

        bool Open(const std::string& worldDir, std::string& error);
        void Close();
        bool IsOpen() const { return db.IsOpen(); }
        const std::string& Directory() const { return dir; }

        ~WorldStorage() override;
        bool LoadChunk(int dimension, ChunkPos pos, ChunkColumn& out) override;
        void SaveChunk(int dimension, ChunkPos pos, const ChunkColumn& column) override;
        // Chunk saves are encoded and written by a background thread, so unloading or autosaving
        // many chunks never stalls the tick. Loads see queued saves before they reach the disk.
        void QueueChunk(int dimension, ChunkPos pos, std::shared_ptr<const ChunkColumn> column) override;
        // Blocks until every queued chunk save is written and committed.
        void DrainSaves();

        bool LoadLevel(LevelInfo& info) const;
        bool SaveLevel(const LevelInfo& info) const;

        bool LoadPlayer(const std::string& id, nbt::Value& data) const;
        void SavePlayer(const std::string& id, const nbt::Value& data);

        bool Flush(std::string* error = nullptr);

        std::vector<nbt::Value> LoadBlockEntities(int dimension, ChunkPos pos) const;
        void SaveBlockEntities(int dimension, ChunkPos pos, const std::vector<nbt::Value>& entities);

        static std::string ChunkKey(int dimension, ChunkPos pos, uint8_t tag);
        static void EncodeSubChunk(const ChunkColumn& column, int section, int yIndex, std::string& out);
        static bool DecodeSubChunk(std::string_view data, uint16_t* cells, uint16_t& uniform,
                                   bool& isUniform, uint16_t* liquid = nullptr, bool* hasLiquid = nullptr);
        static std::vector<uint8_t> LevelDat(const LevelInfo& info);
        static bool ParseLevelDat(const std::vector<uint8_t>& bytes, LevelInfo& info);

    private:
        using SaveKey = std::tuple<int, int32_t, int32_t>;
        void SaverLoop();

        std::string dir;
        LevelDb db;
        std::mutex saveMutex;
        std::condition_variable saveWake, saveIdle;
        std::deque<SaveKey> saveQueue;
        std::map<SaveKey, std::shared_ptr<const ChunkColumn>> pendingSaves;
        size_t savesInFlight = 0;
        bool saverStopping = false;
        std::thread saver;
    };
}
