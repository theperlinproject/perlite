#pragma once
#include "world/level/chunk/Chunk.hpp"
#include "world/level/levelgen/Generator.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace perlite {

    class ChunkStore;

    struct ChunkEntry {
        ChunkColumn column;
        bool fresh = false;

        std::vector<uint8_t> payload;
        uint64_t payloadVersion = ~0ull;

        // The chunk's packet, sealed (compressed) once and sent as is to every player who needs it.
        std::vector<uint8_t> sealed;
        uint64_t sealedVersion = ~0ull;
        uint32_t sealedSettings = ~0u;
    };

    class ChunkManager {
    public:

        explicit ChunkManager(std::unique_ptr<TerrainBackend> backend, size_t maxThreads = 0, int dimension = kOverworld);
        int DimensionId() const { return dimension; }
        ~ChunkManager();

        const char* BackendName() const { return backend->Name(); }

        TerrainBackend& Backend() { return *backend; }

        std::shared_ptr<ChunkEntry> Get(ChunkPos pos);
        std::shared_ptr<ChunkEntry> Peek(ChunkPos pos) const;
        std::shared_ptr<ChunkEntry> Load(ChunkPos pos);

        void Pump();
        std::vector<ChunkPos> TakeReady() { return std::exchange(ready, {}); }

        const std::vector<uint8_t>& Payload(ChunkEntry& entry) const;

        uint16_t GetBlock(int x, int y, int z) const;
        // The loaded column of a chunk, or null.
        const ChunkColumn* Column(int chunkX, int chunkZ) const { return FindColumn(chunkX, chunkZ); }

        bool SetBlock(int x, int y, int z, uint16_t block);

        size_t Loaded() const { return chunks.size(); }

        struct Viewer {
            ChunkPos center;
            int radius = 0;
        };
        size_t UnloadFar(const std::vector<Viewer>& viewers, int margin);
        // The players' positions and view radii. Workers then generate the queued chunk nearest a
        // player first, and drop queued chunks no player is within reach of any more (they are
        // requested again if a player comes back). Without viewers the queue is first in, first out.
        void SetViewers(std::vector<Viewer> viewers);

        void SetStore(ChunkStore* s) { store = s; }
        ChunkStore* Store() const { return store; }
        size_t SaveDirty();
        size_t DirtyCount() const { return dirty.size(); }
        size_t Pending() const;

    private:
        void Worker();
        std::vector<std::shared_ptr<const ChunkColumn>> Raws(const std::vector<ChunkPos>& wanted);
        void Finish(const std::vector<ChunkPos>& batch, std::vector<ChunkColumn>& columns);

        bool FromStore(ChunkPos pos, ChunkColumn& out);

        std::unique_ptr<TerrainBackend> backend;
        int dimension = kOverworld;
        ChunkStore* store = nullptr;
        std::unordered_set<ChunkPos, ChunkPosHash> dirty;
        std::unordered_map<ChunkPos, std::shared_ptr<ChunkEntry>, ChunkPosHash> chunks;
        // Bumped whenever a chunk leaves `chunks`, so cached pointers to columns can be trusted.
        std::atomic<uint64_t> eraseEpoch{0};
        const uint64_t instanceId = NextInstanceId();
        static uint64_t NextInstanceId() {
            static std::atomic<uint64_t> next{1};
            return next.fetch_add(1);
        }
        const ChunkColumn* FindColumn(int chunkX, int chunkZ) const;
        std::unordered_set<ChunkPos, ChunkPosHash> edited;
        std::unordered_set<ChunkPos, ChunkPosHash> everReady;
        std::unordered_set<ChunkPos, ChunkPosHash> requested;
        std::vector<ChunkPos> ready;

        mutable std::mutex mutex;
        std::condition_variable wake;
        std::deque<ChunkPos> queue;
        std::vector<Viewer> viewers;
        std::vector<ChunkPos> dropped;
        std::unordered_set<ChunkPos, ChunkPosHash> pinned;  // asked for by Load: never dropped
        bool TakeNearest(std::vector<ChunkPos>& batch, size_t take);
        std::vector<std::pair<ChunkPos, std::shared_ptr<ChunkEntry>>> done;
        bool stopping = false;
        std::unordered_map<ChunkPos, std::shared_ptr<const ChunkColumn>, ChunkPosHash> raw;
        std::unordered_set<ChunkPos, ChunkPosHash> rawPending;
        std::deque<ChunkPos> rawOrder;
        std::condition_variable rawReady;
        size_t rawCap = 192;

        std::vector<std::thread> workers;
    };
}
