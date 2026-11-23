// The End's generated features: the obsidian pillars of the dragon fight, chorus plants and the small
// floating islands. Positions and sizes follow the Java Edition algorithms the terrain is ported
// from, seeded the same way.
#include "world/level/end/EndGen.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/Generator.hpp"
#include "world/level/levelgen/vanilla/JavaRandom.hpp"

#include <cmath>
#include <map>
#include <mutex>
#include <numbers>

namespace perlite::theend {
    namespace {
        using vanilla::WorldRandom;

        uint16_t Block(const char* name) { return BlockCatalog::Instance().DefaultBlock(name); }

        // Java's Util.shuffle over 0..n-1 with a java.util.Random.
        std::vector<int> Shuffled(int n, WorldRandom& random) {
            std::vector<int> list(static_cast<size_t>(n));
            for (int i = 0; i < n; i++)
                list[static_cast<size_t>(i)] = i;
            for (int i = n; i > 1; i--) {
                const int j = random.NextInt(i);
                std::swap(list[static_cast<size_t>(i - 1)], list[static_cast<size_t>(j)]);
            }
            return list;
        }

        int Floor(double v) { return static_cast<int>(std::floor(v)); }

        // The decoration seed of a chunk, as Java's WorldgenRandom.setDecorationSeed.
        int64_t DecorationSeed(int64_t seed, int minX, int minZ) {
            WorldRandom r = WorldRandom::Legacy(seed);
            const int64_t a = static_cast<int64_t>(r.NextLong() | 1), b = static_cast<int64_t>(r.NextLong() | 1);
            return static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>(minX)) * static_cast<uint64_t>(a) +
                                         static_cast<uint64_t>(static_cast<int64_t>(minZ)) * static_cast<uint64_t>(b)) ^
                                        static_cast<uint64_t>(seed));
        }

        // Block reads and writes for one chunk being decorated: reads see the chunk itself as it is
        // being written and the undecorated neighbours; writes land only inside the chunk.
        struct View {
            int chunkX, chunkZ;
            const ChunkColumn* const* region;
            ChunkColumn& out;
            uint16_t Get(int x, int y, int z) const {
                const int dx = (x >> 4) - chunkX, dz = (z >> 4) - chunkZ;
                if (dx == 0 && dz == 0)
                    return out.Get(x & 15, y, z & 15);
                if (dx < -kDecorationReach || dx > kDecorationReach || dz < -kDecorationReach || dz > kDecorationReach)
                    return 0;
                const ChunkColumn* c = region[DecorationSlot(dx, dz)];
                return c ? c->Get(x & 15, y, z & 15) : 0;
            }
            void Set(int x, int y, int z, uint16_t block) {
                if ((x >> 4) == chunkX && (z >> 4) == chunkZ && y >= 0 && y < 256)
                    out.Set(x & 15, y, z & 15, block);
            }
            bool Empty(int x, int y, int z) const { return Get(x, y, z) == BlockRegistry::kAir; }
            // The highest non-air block + 1 (the motion-blocking heightmap of the end's terrain).
            int Top(int x, int z) const {
                for (int y = 255; y >= 0; y--)
                    if (Get(x, y, z) != BlockRegistry::kAir)
                        return y + 1;
                return 0;
            }
            int BiomeAt(int x, int z) const {
                const int dx = (x >> 4) - chunkX, dz = (z >> 4) - chunkZ;
                const ChunkColumn* c = dx == 0 && dz == 0 ? &out : region[DecorationSlot(dx, dz)];
                return c ? c->GenBiome(x & 15, z & 15) : -1;
            }
        };

        constexpr int kHorizontal[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; // north east south west

        bool AllNeighboursEmpty(const View& v, int x, int y, int z, int skip) {
            for (int d = 0; d < 4; d++)
                if (d != skip && !v.Empty(x + kHorizontal[d][0], y, z + kHorizontal[d][1]))
                    return false;
            return true;
        }

        // ChorusFlowerBlock.growTreeRecursive.
        void GrowChorus(View& v, int bx, int by, int bz, WorldRandom& random, int ox, int oz, int reach, int depth) {
            static const uint16_t plant = Block("minecraft:chorus_plant");
            static const uint16_t flower =
                BlockCatalog::Instance().Resolve("minecraft:chorus_flower[age=5]") != BlockRegistry::kAir
                    ? BlockCatalog::Instance().Resolve("minecraft:chorus_flower[age=5]")
                    : Block("minecraft:chorus_flower");
            int height = random.NextInt(4) + 1;
            if (depth == 0)
                height++;
            for (int j = 0; j < height; j++) {
                if (!AllNeighboursEmpty(v, bx, by + j + 1, bz, -1))
                    return;
                v.Set(bx, by + j + 1, bz, plant);
                v.Set(bx, by + j, bz, plant);
            }
            bool branched = false;
            if (depth < 4) {
                int branches = random.NextInt(4);
                if (depth == 0)
                    branches++;
                for (int k = 0; k < branches; k++) {
                    const int d = random.NextInt(4);
                    const int nx = bx + kHorizontal[d][0], ny = by + height, nz = bz + kHorizontal[d][1];
                    if (std::abs(nx - ox) < reach && std::abs(nz - oz) < reach && v.Empty(nx, ny, nz) && v.Empty(nx, ny - 1, nz) &&
                        AllNeighboursEmpty(v, nx, ny, nz, (d + 2) % 4)) {
                        branched = true;
                        v.Set(nx, ny, nz, plant);
                        v.Set(nx - kHorizontal[d][0], ny, nz - kHorizontal[d][1], plant);
                        GrowChorus(v, nx, ny, nz, random, ox, oz, reach, depth + 1);
                    }
                }
            }
            if (!branched)
                v.Set(bx, by + height, bz, flower);
        }

        void SpikeInto(const Spike& s, int chunkX, int chunkZ, ChunkColumn& out) {
            const int r = s.radius + 2;
            if (s.x + r < chunkX * 16 || s.x - r > chunkX * 16 + 15 || s.z + r < chunkZ * 16 || s.z - r > chunkZ * 16 + 15)
                return;
            PlaceSpike(s, [&](int x, int y, int z, uint16_t block) {
                if ((x >> 4) == chunkX && (z >> 4) == chunkZ)
                    out.Set(x & 15, y, z & 15, block);
            });
        }
    }

    void PlaceSpike(const Spike& s, const std::function<void(int, int, int, uint16_t)>& set) {
        static const uint16_t obsidian = Block("minecraft:obsidian");
        static const uint16_t bedrock = Block("minecraft:bedrock");
        static const uint16_t bars = Block("minecraft:iron_bars");
        const int r = s.radius;
        for (int x = s.x - r; x <= s.x + r; x++)
            for (int z = s.z - r; z <= s.z + r; z++) {
                const int dx = x - s.x, dz = z - s.z;
                const bool inside = dx * dx + dz * dz <= r * r + 1;
                for (int y = 0; y <= s.height + 10; y++) {
                    if (inside && y < s.height)
                        set(x, y, z, obsidian);
                    else if (y > 65)
                        set(x, y, z, BlockRegistry::kAir);
                }
            }
        if (s.guarded)
            for (int k = -2; k <= 2; k++)
                for (int l = -2; l <= 2; l++)
                    for (int h = 0; h <= 3; h++)
                        if (std::abs(k) == 2 || std::abs(l) == 2 || h == 3)
                            set(s.x + k, s.height + h, s.z + l, bars);
        set(s.x, s.height, s.z, bedrock);
    }

    const std::vector<Spike>& Spikes(int64_t seed) {
        static std::mutex mutex;
        static std::map<int64_t, std::vector<Spike>> cache;
        std::lock_guard lock(mutex);
        auto& spikes = cache[seed];
        if (!spikes.empty())
            return spikes;
        WorldRandom random = WorldRandom::Legacy(seed);
        WorldRandom order = WorldRandom::Legacy(static_cast<int64_t>(random.NextLong() & 65535ull));
        const std::vector<int> sizes = Shuffled(10, order);
        for (int j = 0; j < 10; j++) {
            const double angle = 2.0 * (-std::numbers::pi + std::numbers::pi / 10.0 * j);
            Spike s;
            s.x = Floor(42.0 * std::cos(angle));
            s.z = Floor(42.0 * std::sin(angle));
            const int size = sizes[static_cast<size_t>(j)];
            s.radius = 2 + size / 3;
            s.height = 76 + size * 3;
            s.guarded = size == 1 || size == 2;
            spikes.push_back(s);
        }
        return spikes;
    }

    const std::vector<GatewaySlot>& Gateways(int64_t seed) {
        static std::mutex mutex;
        static std::map<int64_t, std::vector<GatewaySlot>> cache;
        std::lock_guard lock(mutex);
        auto& slots = cache[seed];
        if (!slots.empty())
            return slots;
        WorldRandom random = WorldRandom::Legacy(seed);
        const std::vector<int> order = Shuffled(20, random);
        for (int i : order) {
            const double angle = 2.0 * (-std::numbers::pi + 0.15707963267948966 * i);
            slots.push_back({Floor(96.0 * std::cos(angle)), 75, Floor(96.0 * std::sin(angle))});
        }
        return slots;
    }

    void PlaceSpikes(int64_t seed, int chunkX, int chunkZ, ChunkColumn& out) {
        for (const Spike& s : Spikes(seed))
            SpikeInto(s, chunkX, chunkZ, out);
    }

    void Decorate(int64_t seed, int chunkX, int chunkZ, const ChunkColumn* const region[], ChunkColumn& out,
                  const Biomes& biomes) {
        View view{chunkX, chunkZ, region, out};
        static const uint16_t endStone = Block("minecraft:end_stone");
        // Features of the chunks around that can reach into this one, each with its own seed.
        for (int oz = -1; oz <= 1; oz++)
            for (int ox = -1; ox <= 1; ox++) {
                const int cx = chunkX + ox, cz = chunkZ + oz;
                const int minX = cx * 16, minZ = cz * 16;
                const int64_t decoration = DecorationSeed(seed, minX, minZ);
                const int centerBiome = view.BiomeAt(minX + 8, minZ + 8);
                // Raw generation: a small floating island, one chunk in 14, between y 55 and 70.
                if (centerBiome == biomes.smallIslands) {
                    WorldRandom r = WorldRandom::Legacy(decoration + 0 + 10000ll * 0);
                    if (r.NextFloat() < 1.0f / 14.0f) {
                        const int x = minX + r.NextInt(16), z = minZ + r.NextInt(16), y = 55 + r.NextInt(16);
                        float size = static_cast<float>(r.NextInt(3)) + 4.0f;
                        for (int i = 0; size > 0.5f; i--) {
                            for (int j = Floor(-size); j <= static_cast<int>(std::ceil(size)); j++)
                                for (int k = Floor(-size); k <= static_cast<int>(std::ceil(size)); k++)
                                    if (static_cast<float>(j * j + k * k) <= (size + 1.0f) * (size + 1.0f))
                                        view.Set(x + j, y + i, z + k, endStone);
                            size -= static_cast<float>(r.NextInt(2)) + 0.5f;
                        }
                    }
                }
                // Vegetal decoration: chorus plants on the highlands, 0 to 4 per chunk.
                if (centerBiome == biomes.highlands) {
                    WorldRandom r = WorldRandom::Legacy(decoration + 0 + 10000ll * 9);
                    const int count = r.NextInt(5);
                    for (int n = 0; n < count; n++) {
                        const int x = minX + r.NextInt(16), z = minZ + r.NextInt(16);
                        const int y = view.Top(x, z);
                        if (y <= 0 || !view.Empty(x, y, z) || view.Get(x, y - 1, z) != endStone)
                            continue;
                        view.Set(x, y, z, Block("minecraft:chorus_plant"));
                        GrowChorus(view, x, y, z, r, x, z, 8, 0);
                    }
                }
            }
        // The pillars are fixed shapes: whatever part of them falls in this chunk.
        if (std::abs(chunkX) <= 4 && std::abs(chunkZ) <= 4)
            PlaceSpikes(seed, chunkX, chunkZ, out);
    }
}
