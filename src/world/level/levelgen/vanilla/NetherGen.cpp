// The nether's features, after the Java Edition ones the terrain is ported from: each chunk's
// features are seeded from the world seed and the chunk, and the chunks around are replayed so that a
// feature crossing a chunk border is the same on both sides.
#include "world/level/levelgen/vanilla/NetherGen.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/Generator.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"
#include "world/level/levelgen/vanilla/JavaRandom.hpp"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace perlite::vanilla {
    namespace {
        uint16_t B(const char* name) { return BlockCatalog::Instance().DefaultBlock(name); }

        int BiomeIndex(const char* java) {
            for (int i = 0; i < kBiomeCount; i++)
                if (std::string(kBiomes[i].java) == java)
                    return i;
            return -1;
        }

        int64_t DecorationSeed(int64_t seed, int minX, int minZ) {
            WorldRandom r = WorldRandom::Legacy(seed);
            const int64_t a = static_cast<int64_t>(r.NextLong() | 1), b = static_cast<int64_t>(r.NextLong() | 1);
            return static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>(minX)) * static_cast<uint64_t>(a) +
                                         static_cast<uint64_t>(static_cast<int64_t>(minZ)) * static_cast<uint64_t>(b)) ^
                                        static_cast<uint64_t>(seed));
        }

        struct Ids {
            uint16_t netherrack, glowstone, fire, soulFire, quartz, gold, magma, debris, gravel, blackstone, soulSand,
                soulSoil, lava, brownMushroom, redMushroom, crimsonNylium, warpedNylium, crimsonStem, warpedStem,
                wartBlock, warpedWart, shroomlight, crimsonRoots, warpedRoots, sprouts, crimsonFungus, warpedFungus,
                weepingVines, twistingVines, basalt, bedrock;
            Ids() {
                netherrack = B("minecraft:netherrack");
                glowstone = B("minecraft:glowstone");
                fire = B("minecraft:fire");
                soulFire = B("minecraft:soul_fire");
                quartz = B("minecraft:quartz_ore");
                gold = B("minecraft:nether_gold_ore");
                magma = B("minecraft:magma");
                debris = B("minecraft:ancient_debris");
                gravel = B("minecraft:gravel");
                blackstone = B("minecraft:blackstone");
                soulSand = B("minecraft:soul_sand");
                soulSoil = B("minecraft:soul_soil");
                lava = B("minecraft:lava");
                brownMushroom = B("minecraft:brown_mushroom");
                redMushroom = B("minecraft:red_mushroom");
                crimsonNylium = B("minecraft:crimson_nylium");
                warpedNylium = B("minecraft:warped_nylium");
                crimsonStem = B("minecraft:crimson_stem");
                warpedStem = B("minecraft:warped_stem");
                wartBlock = B("minecraft:nether_wart_block");
                warpedWart = B("minecraft:warped_wart_block");
                shroomlight = B("minecraft:shroomlight");
                crimsonRoots = B("minecraft:crimson_roots");
                warpedRoots = B("minecraft:warped_roots");
                sprouts = B("minecraft:nether_sprouts");
                crimsonFungus = B("minecraft:crimson_fungus");
                warpedFungus = B("minecraft:warped_fungus");
                weepingVines = B("minecraft:weeping_vines");
                twistingVines = B("minecraft:twisting_vines");
                basalt = B("minecraft:basalt");
                bedrock = B("minecraft:bedrock");
            }
        };
        const Ids& I() {
            static const Ids ids;
            return ids;
        }

        struct View {
            int chunkX, chunkZ;
            const ChunkColumn* const* region;
            ChunkColumn& out;
            uint16_t Get(int x, int y, int z) const {
                if (y < 0 || y > 127)
                    return I().bedrock;
                const int dx = (x >> 4) - chunkX, dz = (z >> 4) - chunkZ;
                if (dx == 0 && dz == 0)
                    return out.Get(x & 15, y, z & 15);
                if (dx < -kDecorationReach || dx > kDecorationReach || dz < -kDecorationReach || dz > kDecorationReach)
                    return I().netherrack;
                const ChunkColumn* c = region[DecorationSlot(dx, dz)];
                return c ? c->Get(x & 15, y, z & 15) : I().netherrack;
            }
            void Set(int x, int y, int z, uint16_t block) {
                if ((x >> 4) == chunkX && (z >> 4) == chunkZ && y > 0 && y < 127)
                    out.Set(x & 15, y, z & 15, block);
            }
            bool Air(int x, int y, int z) const { return Get(x, y, z) == BlockRegistry::kAir; }
            int BiomeAt(int x, int y, int z) const {
                const int dx = (x >> 4) - chunkX, dz = (z >> 4) - chunkZ;
                const ChunkColumn* c = dx == 0 && dz == 0 ? &out
                                       : (std::abs(dx) <= kDecorationReach && std::abs(dz) <= kDecorationReach)
                                           ? region[DecorationSlot(dx, dz)]
                                           : nullptr;
                if (!c)
                    return -1;
                return c->HasQuartBiomes() ? c->QuartBiome((x & 15) >> 2, ((y - kMinY) >> 2), (z & 15) >> 2) : c->GenBiome(x & 15, z & 15);
            }
        };

        // Java's OreFeature: a vein of spheres strung along a line of `size` steps.
        void Ore(View& v, WorldRandom& r, int ox, int oy, int oz, int size, const std::vector<uint16_t>& replace,
                 uint16_t ore, float discardOnAir) {
            const float dir = r.NextFloat() * 3.14159265358979323846f;
            const float spread = static_cast<float>(size) / 8.0f;
            const double x0 = ox + std::sin(static_cast<double>(dir)) * spread, x1 = ox - std::sin(static_cast<double>(dir)) * spread;
            const double z0 = oz + std::cos(static_cast<double>(dir)) * spread, z1 = oz - std::cos(static_cast<double>(dir)) * spread;
            const double y0 = oy + r.NextInt(3) - 2, y1 = oy + r.NextInt(3) - 2;
            for (int i = 0; i < size; i++) {
                const float step = static_cast<float>(i) / static_cast<float>(size);
                const double xx = x0 + step * (x1 - x0), yy = y0 + step * (y1 - y0), zz = z0 + step * (z1 - z0);
                const double ss = r.NextDouble() * size / 16.0;
                const double rad = ((std::sin(3.14159265358979323846 * step) + 1.0) * ss + 1.0) / 2.0;
                for (int x = static_cast<int>(std::floor(xx - rad)); x <= static_cast<int>(std::floor(xx + rad)); x++)
                    for (int y = static_cast<int>(std::floor(yy - rad)); y <= static_cast<int>(std::floor(yy + rad)); y++)
                        for (int z = static_cast<int>(std::floor(zz - rad)); z <= static_cast<int>(std::floor(zz + rad)); z++) {
                            const double dx = (x + 0.5 - xx) / rad, dy = (y + 0.5 - yy) / rad, dz = (z + 0.5 - zz) / rad;
                            if (dx * dx + dy * dy + dz * dz >= 1.0)
                                continue;
                            const uint16_t here = v.Get(x, y, z);
                            bool ok = false;
                            for (uint16_t t : replace)
                                ok |= here == t;
                            if (!ok)
                                continue;
                            if (discardOnAir > 0.0f && r.NextFloat() < discardOnAir &&
                                (v.Air(x + 1, y, z) || v.Air(x - 1, y, z) || v.Air(x, y + 1, z) || v.Air(x, y - 1, z) ||
                                 v.Air(x, y, z + 1) || v.Air(x, y, z - 1)))
                                continue;
                            v.Set(x, y, z, ore);
                        }
            }
        }

        // GlowstoneFeature: hangs from netherrack ceilings, grown by 1500 tries that touch exactly one
        // glowstone neighbour.
        void Glowstone(View& v, WorldRandom& r, int x, int y, int z) {
            if (!v.Air(x, y, z) || v.Get(x, y + 1, z) != I().netherrack)
                return;
            v.Set(x, y, z, I().glowstone);
            for (int i = 0; i < 1500; i++) {
                const int px = x + r.NextInt(8) - r.NextInt(8), py = y - r.NextInt(12), pz = z + r.NextInt(8) - r.NextInt(8);
                if (!v.Air(px, py, pz))
                    continue;
                int touching = 0;
                static constexpr int kN[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (const auto& d : kN)
                    touching += v.Get(px + d[0], py + d[1], pz + d[2]) == I().glowstone;
                if (touching == 1)
                    v.Set(px, py, pz, I().glowstone);
            }
        }

        // RandomPatch of a simple block: `tries` attempts spread around the position.
        template <class F> void Patch(View& v, WorldRandom& r, int x, int y, int z, int tries, int xz, int yy, F&& place) {
            for (int i = 0; i < tries; i++) {
                const int px = x + r.NextInt(xz + 1) - r.NextInt(xz + 1), py = y + r.NextInt(yy + 1) - r.NextInt(yy + 1),
                          pz = z + r.NextInt(xz + 1) - r.NextInt(xz + 1);
                place(px, py, pz);
            }
        }

        // SpringFeature (lava): a source in rock with exactly one open side (open) or none (closed).
        void Spring(View& v, int x, int y, int z, bool closed) {
            if (v.Get(x, y + 1, z) != I().netherrack || v.Get(x, y, z) != I().netherrack && !v.Air(x, y, z))
                return;
            int rock = 0, air = 0;
            static constexpr int kSides[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
            for (const auto& d : kSides) {
                const uint16_t n = v.Get(x + d[0], y + d[1], z + d[2]);
                rock += n == I().netherrack;
                air += n == BlockRegistry::kAir;
            }
            if (closed ? (rock == 5) : (rock == 4 && air == 1))
                v.Set(x, y, z, I().lava);
        }

        // HugeFungusFeature: a stem of 4 to 13 blocks (some double width) with a cap of wart blocks
        // and shroomlights.
        void HugeFungus(View& v, WorldRandom& r, int x, int y, int z, bool warped) {
            const uint16_t nylium = warped ? I().warpedNylium : I().crimsonNylium;
            if (v.Get(x, y - 1, z) != nylium)
                return;
            int height = r.NextIntBetweenInclusive(4, 13);
            if (r.NextInt(12) == 0)
                height *= 2;
            if (y + height + 1 >= 127)
                return;
            const bool thick = r.NextFloat() < 0.06f;
            const uint16_t stem = warped ? I().warpedStem : I().crimsonStem;
            const uint16_t hat = warped ? I().warpedWart : I().wartBlock;
            const int radius = thick ? 1 : 0;
            for (int dx = -radius; dx <= radius; dx++)
                for (int dz = -radius; dz <= radius; dz++) {
                    const bool corner = thick && std::abs(dx) == radius && std::abs(dz) == radius;
                    for (int h = 0; h < height; h++)
                        if (!corner || r.NextFloat() < 0.1f)
                            if (v.Air(x + dx, y + h, z + dz) || v.Get(x + dx, y + h, z + dz) == (warped ? I().warpedRoots : I().crimsonRoots))
                                v.Set(x + dx, y + h, z + dz, stem);
                }
            const int capTop = y + height;
            const int capBottom = capTop - std::min(r.NextInt(1 + height / 3) + 5, height);
            for (int py = capBottom; py <= capTop; py++) {
                int capRadius = py < capTop - r.NextInt(3) ? 2 : 1;
                if (height > 8 && py < capBottom + 4)
                    capRadius = 3;
                if (thick)
                    capRadius++;
                for (int dx = -capRadius; dx <= capRadius; dx++)
                    for (int dz = -capRadius; dz <= capRadius; dz++) {
                        const bool edgeX = std::abs(dx) == capRadius, edgeZ = std::abs(dz) == capRadius;
                        if (edgeX && edgeZ && r.NextFloat() > 0.2f)
                            continue;
                        if (!v.Air(x + dx, py, z + dz))
                            continue;
                        if (py >= capTop - 1 || edgeX || edgeZ) {
                            if (r.NextFloat() < 0.1f)
                                v.Set(x + dx, py, z + dz, I().shroomlight);
                            else
                                v.Set(x + dx, py, z + dz, hat);
                        } else if (!warped && r.NextFloat() < 0.1f) {
                            // weeping vines under the cap
                            for (int k = 0; k < 1 + r.NextInt(4) && v.Air(x + dx, py - 1 - k, z + dz); k++)
                                v.Set(x + dx, py - 1 - k, z + dz, I().weepingVines);
                        }
                    }
            }
        }
    }

    void DecorateNether(int64_t seed, int chunkX, int chunkZ, const ChunkColumn* const region[], ChunkColumn& out) {
        static const int wastes = BiomeIndex("nether_wastes"), crimson = BiomeIndex("crimson_forest"),
                         warped = BiomeIndex("warped_forest"), soulValley = BiomeIndex("soul_sand_valley"),
                         deltas = BiomeIndex("basalt_deltas");
        View v{chunkX, chunkZ, region, out};
        const Ids& id = I();
        for (int oz = -1; oz <= 1; oz++)
            for (int ox = -1; ox <= 1; ox++) {
                const int minX = (chunkX + ox) * 16, minZ = (chunkZ + oz) * 16;
                const int64_t decoration = DecorationSeed(seed, minX, minZ);
                const int biome = v.BiomeAt(minX + 8, 64, minZ + 8);
                int index = 0;
                const auto feature = [&](int step) {
                    return WorldRandom::Legacy(decoration + index++ + 10000ll * step);
                };
                const auto rand16 = [](WorldRandom& r) { return r.NextInt(16); };

                // Fire on netherrack (soul fire on soul soil in the valley).
                {
                    WorldRandom r = feature(9);
                    const int count = r.NextInt(r.NextInt(4) + 1) + 1;
                    for (int n = 0; n < count; n++) {
                        const int x = minX + rand16(r), z = minZ + rand16(r), y = 4 + r.NextInt(120);
                        const bool soul = biome == soulValley;
                        Patch(v, r, x, y, z, 64, 7, 3, [&](int px, int py, int pz) {
                            const uint16_t below = v.Get(px, py - 1, pz);
                            if (v.Air(px, py, pz) && (soul ? (below == id.soulSoil || below == id.soulSand) : below == id.netherrack))
                                v.Set(px, py, pz, soul ? id.soulFire : id.fire);
                        });
                    }
                }
                // Glowstone: a few blobs anywhere, ten more biased to the bottom.
                {
                    WorldRandom r = feature(7);
                    const int extra = r.NextInt(r.NextInt(10) + 1);
                    for (int n = 0; n < extra; n++)
                        Glowstone(v, r, minX + rand16(r), 4 + r.NextInt(120), minZ + rand16(r));
                    for (int n = 0; n < 10; n++)
                        Glowstone(v, r, minX + rand16(r), r.NextInt(128), minZ + rand16(r));
                }
                // Mushrooms (wastes and valleys), on the floor.
                if (biome == wastes || biome == soulValley) {
                    WorldRandom r = feature(9);
                    for (uint16_t shroom : {id.brownMushroom, id.redMushroom})
                        if (r.NextInt(2) == 0) {
                            const int x = minX + rand16(r), z = minZ + rand16(r), y = r.NextInt(128);
                            Patch(v, r, x, y, z, 64, 7, 3, [&](int px, int py, int pz) {
                                if (v.Air(px, py, pz) && v.Get(px, py - 1, pz) == id.netherrack)
                                    v.Set(px, py, pz, shroom);
                            });
                        }
                }
                // Ores.
                {
                    WorldRandom r = feature(6);
                    const std::vector<uint16_t> rack{id.netherrack};
                    for (int n = 0; n < 4; n++) // magma, y 27-36
                        Ore(v, r, minX + rand16(r), 27 + r.NextInt(10), minZ + rand16(r), 33, rack, id.magma, 0.0f);
                    for (int n = 0; n < 2; n++) // gravel, y 5-41
                        Ore(v, r, minX + rand16(r), 5 + r.NextInt(37), minZ + rand16(r), 33, rack, id.gravel, 0.0f);
                    for (int n = 0; n < 2; n++) // blackstone, y 5-31
                        Ore(v, r, minX + rand16(r), 5 + r.NextInt(27), minZ + rand16(r), 33, rack, id.blackstone, 0.0f);
                    const int goldCount = biome == deltas ? 20 : 10, quartzCount = biome == deltas ? 32 : 16;
                    for (int n = 0; n < goldCount; n++) // gold, y 10-117
                        Ore(v, r, minX + rand16(r), 10 + r.NextInt(108), minZ + rand16(r), 10, rack, id.gold, 0.0f);
                    for (int n = 0; n < quartzCount; n++) // quartz, y 10-117
                        Ore(v, r, minX + rand16(r), 10 + r.NextInt(108), minZ + rand16(r), 14, rack, id.quartz, 0.0f);
                    if (biome == soulValley)
                        for (int n = 0; n < 12; n++) // soul sand, y 0-31
                            Ore(v, r, minX + rand16(r), r.NextInt(32), minZ + rand16(r), 12, rack, id.soulSand, 0.0f);
                    // Ancient debris: one large vein low down, one small anywhere; buried.
                    const std::vector<uint16_t> debrisIn{id.netherrack, id.basalt, id.blackstone};
                    Ore(v, r, minX + rand16(r), 8 + (r.NextInt(9) + r.NextInt(9)), minZ + rand16(r), 3, debrisIn, id.debris, 1.0f);
                    Ore(v, r, minX + rand16(r), 8 + r.NextInt(112), minZ + rand16(r), 2, debrisIn, id.debris, 1.0f);
                }
                // Lava springs: eight open to the air, sixteen closed in rock.
                {
                    WorldRandom r = feature(8);
                    for (int n = 0; n < 8; n++)
                        Spring(v, minX + rand16(r), 4 + r.NextInt(120), minZ + rand16(r), false);
                    for (int n = 0; n < 16; n++)
                        Spring(v, minX + rand16(r), 10 + r.NextInt(108), minZ + rand16(r), true);
                }
                // Fungus forests: the huge fungi on every floor, and roots, sprouts and vines.
                if (biome == crimson || biome == warped) {
                    WorldRandom r = feature(9);
                    const bool w = biome == warped;
                    for (int n = 0; n < 8; n++) {
                        const int x = minX + rand16(r), z = minZ + rand16(r);
                        for (int y = 32; y < 120; y++)
                            if (v.Air(x, y, z) && v.Get(x, y - 1, z) == (w ? id.warpedNylium : id.crimsonNylium)) {
                                HugeFungus(v, r, x, y, z, w);
                                break;
                            }
                    }
                    for (int n = 0; n < 6; n++) {
                        const int x = minX + rand16(r), z = minZ + rand16(r), y = r.NextInt(128);
                        Patch(v, r, x, y, z, 64, 7, 3, [&](int px, int py, int pz) {
                            if (!v.Air(px, py, pz) || v.Get(px, py - 1, pz) != (w ? id.warpedNylium : id.crimsonNylium))
                                return;
                            const int pick = r.NextInt(w ? 3 : 4);
                            const uint16_t plant = w ? (pick == 0 ? id.sprouts : pick == 1 ? id.warpedFungus : id.warpedRoots)
                                                     : (pick == 0 ? id.crimsonFungus : id.crimsonRoots);
                            v.Set(px, py, pz, plant);
                        });
                    }
                    for (int n = 0; n < 10; n++) {
                        const int x = minX + rand16(r), z = minZ + rand16(r), y = r.NextInt(128);
                        if (w) {
                            if (v.Air(x, y, z) && v.Get(x, y - 1, z) == id.warpedNylium)
                                for (int k = 0; k < 1 + r.NextInt(8) && v.Air(x, y + k, z); k++)
                                    v.Set(x, y + k, z, id.twistingVines);
                        } else if (v.Air(x, y, z) && v.Get(x, y + 1, z) == id.netherrack) {
                            for (int k = 0; k < 1 + r.NextInt(8) && v.Air(x, y - k, z); k++)
                                v.Set(x, y - k, z, id.weepingVines);
                        }
                    }
                }
                // Basalt deltas: basalt columns rising from the floor.
                if (biome == deltas) {
                    WorldRandom r = feature(2);
                    for (int n = 0; n < 4; n++) {
                        const int x = minX + rand16(r), z = minZ + rand16(r);
                        for (int y = 32; y < 110; y++)
                            if (v.Air(x, y, z) && !v.Air(x, y - 1, z)) {
                                const int h = 1 + r.NextInt(5), reach = r.NextInt(3);
                                for (int dx = -reach; dx <= reach; dx++)
                                    for (int dz = -reach; dz <= reach; dz++)
                                        for (int k = 0; k < h - std::abs(dx) - std::abs(dz); k++)
                                            if (v.Air(x + dx, y + k, z + dz))
                                                v.Set(x + dx, y + k, z + dz, id.basalt);
                                break;
                            }
                    }
                }
            }
    }
}
