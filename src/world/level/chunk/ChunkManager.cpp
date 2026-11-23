#include "common/Log.hpp"
#include <cstdlib>
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/block/Waterlog.hpp"

#include <climits>
#include "world/level/storage/WorldStorage.hpp"

#include <algorithm>
#include <chrono>
#include <span>

#if defined(_WIN32)
extern "C" {
__declspec(dllimport) void* __stdcall GetCurrentThread(void);
__declspec(dllimport) int __stdcall SetThreadPriority(void*, int);
}
#elif defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace perlite {
    namespace {
        void YieldToTick() {
#if defined(_WIN32)
            SetThreadPriority(GetCurrentThread(), -1);
#elif defined(__linux__) || defined(__ANDROID__)
            setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 4);
#endif
        }
    }

    ChunkManager::ChunkManager(std::unique_ptr<TerrainBackend> b, size_t maxThreads, int dimensionId)
        : backend(std::move(b)), dimension(dimensionId) {
        size_t threads = 1;
        if (backend->ThreadSafe()) {
            const unsigned hw = std::thread::hardware_concurrency();

            // Workers run at a lower priority than the tick thread, so on small machines every core
            // can generate; on bigger ones one core is left to the tick and the network.
            threads = std::clamp<size_t>(hw <= 4 ? hw : hw - 1, 1, 16);
            if (maxThreads > 0)
                threads = std::min(threads, maxThreads);
        }
        for (size_t i = 0; i < threads; i++)
            workers.emplace_back([this, n = workers.size() + 1] {
                SetLogThreadName("Worker-Main-" + std::to_string(n));
                Worker();
            });
    }

    ChunkManager::~ChunkManager() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        rawReady.notify_all();
        for (std::thread& w : workers)
            if (w.joinable())
                w.join();
    }

    void ChunkManager::Worker() {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), "server-chunkgen");
#endif
        YieldToTick();
        for (;;) {
            std::vector<ChunkPos> batch;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping)
                    return;
                const size_t take = std::min(queue.size(), std::max<size_t>(1, backend->PreferredBatch()));
                if (!TakeNearest(batch, take)) {
                    batch.assign(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(take));
                    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(take));
                }
                if (batch.empty())
                    continue;
            }
            if (store) {
                std::vector<ChunkPos> fresh;
                for (const ChunkPos& p : batch) {
                    auto entry = std::make_shared<ChunkEntry>();
                    if (FromStore(p, entry->column)) {
                        std::lock_guard lock(mutex);
                        done.emplace_back(p, std::move(entry));
                    } else {
                        fresh.push_back(p);
                    }
                }
                batch.swap(fresh);
                if (batch.empty())
                    continue;
            }
            std::vector<ChunkColumn> columns(batch.size());
            if (backend->Decorates())
                Finish(batch, columns);
            else
                backend->Generate(batch, columns);
            for (ChunkColumn& c : columns) {
                waterlog::FillGenerated(c);
                c.generatedNow = true;
            }
            std::lock_guard lock(mutex);
            for (size_t i = 0; i < batch.size(); i++) {
                auto entry = std::make_shared<ChunkEntry>();
                entry->column = std::move(columns[i]);
                entry->fresh = true;
                done.emplace_back(batch[i], std::move(entry));
            }
        }
    }

    std::vector<std::shared_ptr<const ChunkColumn>> ChunkManager::Raws(const std::vector<ChunkPos>& wanted) {
        std::vector<std::shared_ptr<const ChunkColumn>> out(wanted.size());
        std::vector<ChunkPos> mine;
        {
            std::lock_guard lock(mutex);
            for (const ChunkPos& p : wanted)
                if (!raw.contains(p) && !rawPending.contains(p)) {
                    rawPending.insert(p);
                    mine.push_back(p);
                }
        }
        const size_t step = std::max<size_t>(1, backend->PreferredBatch());
        for (size_t i = 0; i < mine.size(); i += step) {
            const size_t n = std::min(step, mine.size() - i);
            std::vector<ChunkColumn> columns(n);
            backend->Generate(std::span<const ChunkPos>(mine.data() + i, n), columns);
            std::lock_guard lock(mutex);
            for (size_t k = 0; k < n; k++) {
                const ChunkPos p = mine[i + k];
                raw[p] = std::make_shared<const ChunkColumn>(std::move(columns[k]));
                rawOrder.push_back(p);
                rawPending.erase(p);
            }
            while (raw.size() > rawCap && !rawOrder.empty()) {
                raw.erase(rawOrder.front());
                rawOrder.pop_front();
            }
            rawReady.notify_all();
        }
        std::unique_lock lock(mutex);
        for (size_t i = 0; i < wanted.size(); i++) {
            rawReady.wait(lock, [&] { return stopping || raw.contains(wanted[i]) || !rawPending.contains(wanted[i]); });
            if (const auto it = raw.find(wanted[i]); it != raw.end()) {
                out[i] = it->second;
            } else if (!stopping) {
                rawPending.insert(wanted[i]);
                lock.unlock();
                std::vector<ChunkColumn> one(1);
                backend->Generate(std::span<const ChunkPos>(&wanted[i], 1), one);
                auto made = std::make_shared<const ChunkColumn>(std::move(one[0]));
                lock.lock();
                raw[wanted[i]] = made;
                rawOrder.push_back(wanted[i]);
                rawPending.erase(wanted[i]);
                rawReady.notify_all();
                out[i] = made;
            }
        }
        return out;
    }

    void ChunkManager::Finish(const std::vector<ChunkPos>& batch, std::vector<ChunkColumn>& columns) {
        std::vector<ChunkPos> wanted;
        for (const ChunkPos& p : batch)
            for (int dz = -kDecorationReach; dz <= kDecorationReach; dz++)
                for (int dx = -kDecorationReach; dx <= kDecorationReach; dx++) {
                    const ChunkPos q{p.x + dx, p.z + dz};
                    if (std::ranges::find(wanted, q) == wanted.end())
                        wanted.push_back(q);
                }
        std::ranges::sort(wanted, [&](const ChunkPos& a, const ChunkPos& b) {
            const bool ia = std::find(batch.begin(), batch.end(), a) != batch.end();
            const bool ib = std::find(batch.begin(), batch.end(), b) != batch.end();
            return ia > ib;
        });
        const auto raws = Raws(wanted);
        for (size_t i = 0; i < batch.size(); i++) {
            const ChunkColumn* region[kDecorationArea] = {};
            for (int dz = -kDecorationReach; dz <= kDecorationReach; dz++)
                for (int dx = -kDecorationReach; dx <= kDecorationReach; dx++) {
                    const ChunkPos q{batch[i].x + dx, batch[i].z + dz};
                    const size_t at = static_cast<size_t>(std::ranges::find(wanted, q) - wanted.begin());
                    region[DecorationSlot(dx, dz)] = raws[at].get();
                }
            const ChunkColumn* middle = region[DecorationSlot(0, 0)];
            if (!middle)
                continue;
            columns[i] = middle->Clone();
            backend->Decorate(batch[i], region, columns[i]);
        }
    }

    void ChunkManager::SetViewers(std::vector<Viewer> v) {
        std::lock_guard lock(mutex);
        viewers = std::move(v);
    }

    // Called with the mutex held. Picks up to `take` queued chunks nearest a viewer and drops those out
    // of every viewer's reach. False when there are no viewers (the caller then takes the oldest).
    bool ChunkManager::TakeNearest(std::vector<ChunkPos>& batch, size_t take) {
        if (viewers.empty())
            return false;
        constexpr int kMargin = 2;
        const auto distance = [&](const ChunkPos& p) {
            int best = INT_MAX;
            for (const Viewer& v : viewers) {
                const int dx = p.x - v.center.x, dz = p.z - v.center.z;
                const int d2 = dx * dx + dz * dz;
                const int reach = v.radius + kMargin;
                if (d2 <= reach * reach)
                    best = std::min(best, d2);
            }
            return best;
        };
        std::vector<std::pair<int, ChunkPos>> keep;
        keep.reserve(queue.size());
        for (const ChunkPos& p : queue) {
            const int d = pinned.contains(p) ? -1 : distance(p);
            if (d == INT_MAX)
                dropped.push_back(p);
            else
                keep.emplace_back(d, p);
        }
        const size_t n = std::min(take, keep.size());
        std::partial_sort(keep.begin(), keep.begin() + static_cast<std::ptrdiff_t>(n), keep.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
        batch.clear();
        for (size_t i = 0; i < n; i++)
            batch.push_back(keep[i].second);
        queue.clear();
        for (size_t i = n; i < keep.size(); i++)
            queue.push_back(keep[i].second);
        return true;
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Get(ChunkPos pos) {
        if (const auto it = chunks.find(pos); it != chunks.end())
            return it->second;
        if (requested.insert(pos).second) {
            {
                std::lock_guard lock(mutex);
                queue.push_back(pos);
            }
            wake.notify_one();
        }
        return nullptr;
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Request(ChunkPos pos) {
        if (const auto it = chunks.find(pos); it != chunks.end())
            return it->second;
        {
            std::lock_guard lock(mutex);
            pinned.insert(pos);
        }
        return Get(pos);
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Peek(ChunkPos pos) const {
        const auto it = chunks.find(pos);
        return it == chunks.end() ? nullptr : it->second;
    }

    void ChunkManager::Pump() {
        std::vector<std::pair<ChunkPos, std::shared_ptr<ChunkEntry>>> ready;
        std::vector<ChunkPos> gone;
        {
            std::lock_guard lock(mutex);
            ready.swap(done);
            gone.swap(dropped);
            for (const auto& r : ready)
                pinned.erase(r.first);
        }
        for (const ChunkPos& pos : gone)
            requested.erase(pos);
        for (auto& [pos, entry] : ready) {
            requested.erase(pos);
            if (chunks.contains(pos))
                continue;
            if (entry->fresh && store)
                dirty.insert(pos);
            chunks[pos] = std::move(entry);
            if (everReady.insert(pos).second)
                this->ready.push_back(pos);
        }
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Load(ChunkPos pos) {
        if (const auto it = chunks.find(pos); it != chunks.end())
            return it->second;
        {
            std::lock_guard lock(mutex);
            pinned.insert(pos);
        }
        if (!backend->ThreadSafe()) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < until) {
                Get(pos);
                Pump();
                if (auto entry = Peek(pos))
                    return entry;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return nullptr;
        }
        if (store) {
            auto stored = std::make_shared<ChunkEntry>();
            if (FromStore(pos, stored->column)) {
                chunks[pos] = stored;
                ready.push_back(pos);
                return stored;
            }
        }
        std::vector<ChunkPos> batch{pos};
        std::vector<ChunkColumn> columns(1);
        if (backend->Decorates())
            Finish(batch, columns);
        else
            backend->Generate(batch, columns);
        waterlog::FillGenerated(columns[0]);
        columns[0].generatedNow = true;
        auto entry = std::make_shared<ChunkEntry>();
        entry->column = std::move(columns[0]);
        chunks[pos] = entry;
        if (store)
            dirty.insert(pos);
        ready.push_back(pos);
        return entry;
    }

    size_t ChunkManager::Pending() const {
        std::lock_guard lock(mutex);
        return queue.size() + done.size();
    }

    const std::vector<uint8_t>& ChunkManager::Payload(ChunkEntry& entry, ChunkPos pos) const {
        if (entry.payloadVersion != entry.column.version) {
            entry.payload.clear();
            entry.column.EncodeNetwork(entry.payload, dimension);
            entry.column.AppendBlockActors(entry.payload, pos.x, pos.z, dimension);
            entry.payloadVersion = entry.column.version;
        }
        return entry.payload;
    }

    // Block lookups come in runs inside one chunk (collision, mob movement, random ticks), so the last
    // column found is remembered per thread.
    const ChunkColumn* ChunkManager::FindColumn(int chunkX, int chunkZ) const {
        struct Last {
            uint64_t owner = 0;
            uint64_t epoch = 0;
            ChunkPos pos{};
            const ChunkColumn* column = nullptr;
        };
        thread_local Last last;
        const uint64_t epoch = eraseEpoch.load(std::memory_order_relaxed);
        if (last.owner == instanceId && last.epoch == epoch && last.pos.x == chunkX && last.pos.z == chunkZ)
            return last.column;
        const auto it = chunks.find({chunkX, chunkZ});
        if (it == chunks.end())
            return nullptr;
        last = {instanceId, epoch, {chunkX, chunkZ}, &it->second->column};
        return last.column;
    }

    uint16_t ChunkManager::GetBlock(int x, int y, int z) const {
        const ChunkColumn* column = FindColumn(x >> 4, z >> 4);
        return column ? column->Get(x & 15, y, z & 15) : 0;
    }

    bool ChunkManager::SetBlock(int x, int y, int z, uint16_t block) {
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return false;
        it->second->column.Set(x & 15, y, z & 15, block);
        edited.insert(it->first);
        if (store)
            dirty.insert(it->first);
        return true;
    }

    uint16_t ChunkManager::GetLiquid(int x, int y, int z) const {
        const ChunkColumn* column = FindColumn(x >> 4, z >> 4);
        return column ? column->GetLiquid(x & 15, y, z & 15) : 0;
    }

    bool ChunkManager::SetLiquid(int x, int y, int z, uint16_t block) {
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return false;
        if (it->second->column.GetLiquid(x & 15, y, z & 15) == block)
            return true;
        it->second->column.SetLiquid(x & 15, y, z & 15, block);
        edited.insert(it->first);
        if (store)
            dirty.insert(it->first);
        return true;
    }

    size_t ChunkManager::UnloadFar(const std::vector<Viewer>& viewers, int margin) {
        const auto far = [&](ChunkPos p) {
            for (const Viewer& v : viewers)
                if (std::abs(p.x - v.center.x) <= v.radius + margin && std::abs(p.z - v.center.z) <= v.radius + margin)
                    return false;
            return true;
        };
        size_t dropped = 0;
        for (auto it = chunks.begin(); it != chunks.end();) {
            if (!far(it->first)) {
                ++it;
                continue;
            }
            if (store) {
                if (dirty.erase(it->first)) {
                    std::shared_ptr<ChunkEntry> entry = it->second;
                    store->QueueChunk(dimension, it->first, std::shared_ptr<const ChunkColumn>(entry, &entry->column));
                }
                edited.erase(it->first);
                eraseEpoch.fetch_add(1, std::memory_order_relaxed);
                it = chunks.erase(it);
                dropped++;
                continue;
            }
            if (edited.contains(it->first)) {
                std::vector<uint8_t>().swap(it->second->payload);
                it->second->payloadVersion = ~0ull;
                ++it;
                continue;
            }
            eraseEpoch.fetch_add(1, std::memory_order_relaxed);
                it = chunks.erase(it);
            dropped++;
        }
        return dropped;
    }

    bool ChunkManager::FromStore(ChunkPos pos, ChunkColumn& out) {
        return store && store->LoadChunk(dimension, pos, out);
    }

    size_t ChunkManager::SaveDirty() {
        if (!store)
            return 0;
        size_t saved = 0;
        for (const ChunkPos& p : dirty)
            if (const auto it = chunks.find(p); it != chunks.end()) {
                store->QueueChunk(dimension, p, std::make_shared<const ChunkColumn>(it->second->column.Clone()));
                saved++;
            }
        dirty.clear();
        return saved;
    }
}
