#pragma once
#include "protocol/NbtTree.hpp"
#include "world/level/chunk/Chunk.hpp"
#include "world/level/storage/LevelDb.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace perlite {

    class ChunkStore {
    public:
        virtual ~ChunkStore() = default;
        virtual bool LoadChunk(int dimension, ChunkPos pos, ChunkColumn& out) = 0;
        virtual void SaveChunk(int dimension, ChunkPos pos, const ChunkColumn& column) = 0;
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

        bool LoadChunk(int dimension, ChunkPos pos, ChunkColumn& out) override;
        void SaveChunk(int dimension, ChunkPos pos, const ChunkColumn& column) override;

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
                                   bool& isUniform);
        static std::vector<uint8_t> LevelDat(const LevelInfo& info);
        static bool ParseLevelDat(const std::vector<uint8_t>& bytes, LevelInfo& info);

    private:
        std::string dir;
        LevelDb db;
    };
}
