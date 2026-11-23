// Differential check of terrain against a world saved by the reference server: loads chunks from a
// Bedrock LevelDB world, generates the same chunks with the vanilla generator and the world's seed,
// and reports how far they agree. Not run by ctest (it needs a reference world):
//
//   vanilla_world_diff <world dir> [minChunkX minChunkZ maxChunkX maxChunkZ]
//
// Reported: exact block agreement, agreement ignoring block states, agreement of the shape
// (air / liquid / solid), surface height differences, and the most common substitutions.
#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"
#include "world/level/storage/WorldStorage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace perlite;

namespace {
    enum class Shape { Air, Liquid, Solid };
    Shape ShapeOf(const std::string& n) {
        if (n == "minecraft:air" || n == "minecraft:cave_air" || n == "minecraft:void_air")
            return Shape::Air;
        if (n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" ||
            n == "minecraft:flowing_lava")
            return Shape::Liquid;
        return Shape::Solid;
    }
    // Terrain shape: what is not part of the ground (plants, trees, vines, snow, decorations) counts
    // as air, so the comparison shows the land, caves and water alone.
    Shape TerrainShapeOf(const std::string& n) {
        static const char* const loose[] = {"leaves", "_log", "wood", "grass", "fern", "flower", "vine", "kelp", "seagrass", "moss_carpet",
                                            "snow_layer", "bush", "mushroom", "dripleaf", "azalea", "lichen", "roots", "spore", "pointed_dripstone",
                                            "sculk_vein", "litter", "petals", "sugar", "reeds", "cactus", "sapling", "tulip", "poppy", "dandelion",
                                            "torch", "pumpkin", "melon", "bamboo", "cocoa", "pickle", "coral", "amethyst_cluster", "bud", "web"};
        if (n.ends_with("grass_block"))
            return Shape::Solid;
        // Underwater plants stand in water.
        if (n.find("kelp") != std::string::npos || n.find("seagrass") != std::string::npos || n.find("sea_pickle") != std::string::npos ||
            n.find("coral") != std::string::npos)
            return Shape::Liquid;
        for (const char* l : loose)
            if (n.find(l) != std::string::npos)
                return n.find("water") != std::string::npos ? Shape::Liquid : Shape::Air;
        return ShapeOf(n);
    }
    int TopSolid(const ChunkColumn& c, int x, int z, const BlockRegistry& r) {
        for (int y = kMinY + kHeight - 1; y >= kMinY; y--)
            if (ShapeOf(r.Name(c.Get(x, y, z))) == Shape::Solid)
                return y;
        return kMinY - 1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vanilla_world_diff <world dir> [minX minZ maxX maxZ]\n");
        return 2;
    }
    BlockCatalog::Instance().LoadBuiltin();
    WorldStorage world;
    std::string error;
    if (!world.Open(argv[1], error)) {
        std::fprintf(stderr, "open: %s\n", error.c_str());
        return 1;
    }
    LevelInfo level;
    world.LoadLevel(level);
    const int x0 = argc > 5 ? std::atoi(argv[2]) : 0, z0 = argc > 5 ? std::atoi(argv[3]) : 0;
    const int x1 = argc > 5 ? std::atoi(argv[4]) : 9, z1 = argc > 5 ? std::atoi(argv[5]) : 9;

    ServerConfig config;
    config.seed = level.seed;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("world %s, seed %llu, chunks %d,%d .. %d,%d\n", argv[1], static_cast<unsigned long long>(level.seed),
                x0, z0, x1, z1);

    // Raw terrain with a border as wide as decoration reads, then decoration of the inner chunks.
    constexpr int border = kDecorationReach;
    std::vector<ChunkPos> rawPos;
    for (int x = x0 - border; x <= x1 + border; x++)
        for (int z = z0 - border; z <= z1 + border; z++)
            rawPos.push_back({x, z});
    std::vector<ChunkColumn> raw(rawPos.size());
    backend->Generate(rawPos, raw);
    const int side = z1 - z0 + 1 + 2 * border;
    const auto rawAt = [&](int x, int z) -> ChunkColumn& {
        return raw[static_cast<size_t>((x - x0 + border) * side + (z - z0 + border))];
    };

    const BlockRegistry& r = BlockRegistry::Instance();
    uint64_t cells = 0, exact = 0, sameName = 0, sameShape = 0, missing = 0;
    uint64_t solidBoth = 0, solidRef = 0, solidOurs = 0;
    std::map<std::pair<std::string, std::string>, uint64_t> confusion;
    std::vector<int> heightDiff;
    std::map<std::string, std::pair<uint64_t, uint64_t>> perBlock;  // reference name -> (count, matched)
    uint64_t quarts = 0, quartsSame = 0, terrainSame = 0;
    std::map<int, std::pair<uint64_t, uint64_t>> terrainByY; // y / 16 -> (cells, differing)
    std::map<std::pair<std::string, std::string>, uint64_t> terrainConfusion;
    std::map<std::pair<std::string, std::string>, uint64_t> biomeConfusion;
    std::map<std::string, std::pair<uint64_t, uint64_t>> blocksOurs; // name -> (reference count, our count)
    for (int cx = x0; cx <= x1; cx++)
        for (int cz = z0; cz <= z1; cz++) {
            ChunkColumn ref;
            if (!world.LoadChunk(kOverworld, {cx, cz}, ref)) {
                missing++;
                continue;
            }
            ChunkColumn ours = rawAt(cx, cz).Clone();
            if (backend->Decorates()) {
                const ChunkColumn* region[kDecorationArea];
                for (int dx = -kDecorationReach; dx <= kDecorationReach; dx++)
                    for (int dz = -kDecorationReach; dz <= kDecorationReach; dz++)
                        region[DecorationSlot(dx, dz)] = &rawAt(cx + dx, cz + dz);
                backend->Decorate({cx, cz}, region, ours);
            }
            for (int qx = 0; qx < 4; qx++)
                for (int qz = 0; qz < 4; qz++)
                    for (int qy = 0; qy < ChunkColumn::kQuartLayers; qy++) {
                        const int a = ref.QuartBiome(qx, qy, qz), b = ours.QuartBiome(qx, qy, qz);
                        quarts++;
                        if (a == b)
                            quartsSame++;
                        else if (a >= 0 && a < vanilla::kBiomeCount && b >= 0 && b < vanilla::kBiomeCount)
                            biomeConfusion[{vanilla::kBiomes[a].bedrock, vanilla::kBiomes[b].bedrock}]++;
                    }
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    heightDiff.push_back(TopSolid(ours, x, z, r) - TopSolid(ref, x, z, r));
                    for (int y = kMinY; y < kMinY + kHeight; y++) {
                        const uint16_t a = ref.Get(x, y, z), b = ours.Get(x, y, z);
                        cells++;
                        const std::string na = r.Name(a), nb = r.Name(b);
                        const Shape sa = ShapeOf(na), sb = ShapeOf(nb);
                        {
                            const Shape ta = TerrainShapeOf(na), tb = TerrainShapeOf(nb);
                            auto& by = terrainByY[(y - kMinY) / 16];
                            by.first++;
                            if (ta == tb)
                                terrainSame++;
                            else {
                                by.second++;
                                terrainConfusion[{na, nb}]++;
                            }
                        }
                        blocksOurs[na].first++;
                        blocksOurs[nb].second++;
                        auto& pb = perBlock[na];
                        pb.first++;
                        if (a == b) {
                            exact++;
                            sameName++;
                            sameShape++;
                            pb.second++;
                        } else {
                            if (na == nb) {
                                sameName++;
                                pb.second++;
                            } else {
                                confusion[{na, nb}]++;
                            }
                            sameShape += sa == sb;
                        }
                        solidRef += sa == Shape::Solid;
                        solidOurs += sb == Shape::Solid;
                        solidBoth += sa == Shape::Solid && sb == Shape::Solid;
                    }
                }
        }
    if (std::getenv("DIFF_BY_Y")) {
        // Per height: share of reference cells that are a given block, and agreement there.
        const std::string want = std::string("minecraft:") + std::getenv("DIFF_BY_Y");
        for (int y = kMinY; y < kMinY + 16; y++) {
            uint64_t refHas = 0, oursHas = 0, both = 0, n = 0;
            for (int cx = x0; cx <= x1; cx++)
                for (int cz = z0; cz <= z1; cz++) {
                    ChunkColumn ref;
                    if (!world.LoadChunk(kOverworld, {cx, cz}, ref))
                        continue;
                    const ChunkColumn& ours = rawAt(cx, cz);
                    for (int x = 0; x < 16; x++)
                        for (int z = 0; z < 16; z++) {
                            const bool a = r.Name(ref.Get(x, y, z)) == want, b = r.Name(ours.Get(x, y, z)) == want;
                            refHas += a;
                            oursHas += b;
                            both += a && b;
                            n++;
                        }
                }
            std::printf("y %4d: reference %5.1f%%  ours %5.1f%%  both %5.1f%%\n", y, 100.0 * refHas / n,
                        100.0 * oursHas / n, 100.0 * both / n);
        }
    }
    if (std::getenv("DIFF_FLOOR")) {
        // Shape of the bedrock floor per column: does bedrock stay contiguous from the bottom?
        std::map<std::string, uint64_t> patterns;
        for (int cx = x0; cx <= x1; cx++)
            for (int cz = z0; cz <= z1; cz++) {
                ChunkColumn ref;
                if (!world.LoadChunk(kOverworld, {cx, cz}, ref))
                    continue;
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++) {
                        std::string p;
                        for (int y = kMinY; y < kMinY + 6; y++)
                            p += r.Name(ref.Get(x, y, z)) == "minecraft:bedrock" ? '#' : '.';
                        patterns[p]++;
                    }
            }
        for (const auto& [p, n] : patterns)
            std::printf("floor %s %llu\n", p.c_str(), static_cast<unsigned long long>(n));
    }
    if (cells == 0) {
        std::fprintf(stderr, "no chunks found in the world (missing %llu)\n", static_cast<unsigned long long>(missing));
        return 1;
    }
    const auto pct = [&](uint64_t n, uint64_t d) { return d ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0; };
    std::printf("compared %llu blocks in %llu chunks (%llu missing from the world)\n",
                static_cast<unsigned long long>(cells), static_cast<unsigned long long>(cells / (16 * 16 * kHeight)),
                static_cast<unsigned long long>(missing));
    std::printf("exact %.2f%%  same block %.2f%%  same shape %.2f%%  solid overlap (IoU) %.2f%%\n", pct(exact, cells),
                pct(sameName, cells), pct(sameShape, cells), pct(solidBoth, solidRef + solidOurs - solidBoth));
    std::sort(heightDiff.begin(), heightDiff.end());
    int zero = 0, within2 = 0;
    for (int d : heightDiff) {
        zero += d == 0;
        within2 += std::abs(d) <= 2;
    }
    std::printf("surface height: same %.1f%%, within 2 %.1f%%, median diff %d, p5 %d, p95 %d\n",
                pct(static_cast<uint64_t>(zero), heightDiff.size()), pct(static_cast<uint64_t>(within2), heightDiff.size()),
                heightDiff[heightDiff.size() / 2], heightDiff[heightDiff.size() / 20],
                heightDiff[heightDiff.size() * 19 / 20]);
    std::vector<std::pair<uint64_t, std::pair<std::string, std::string>>> top;
    for (const auto& [k, v] : confusion)
        top.push_back({v, k});
    std::sort(top.rbegin(), top.rend());
    std::printf("most common differences (reference -> ours):\n");
    for (size_t i = 0; i < std::min<size_t>(20, top.size()); i++)
        std::printf("  %9llu  %s -> %s\n", static_cast<unsigned long long>(top[i].first), top[i].second.first.c_str() + 10,
                    top[i].second.second.c_str() + 10);
    std::printf("terrain shape (air / liquid / solid, plants and trees as air): %.3f%% the same\n", pct(terrainSame, cells));
    for (const auto& [sec, v] : terrainByY)
        if (v.second)
            std::printf("  y %4d..%4d: %.3f%% differ\n", kMinY + sec * 16, kMinY + sec * 16 + 15, pct(v.second, v.first));
    {
        std::vector<std::pair<uint64_t, std::pair<std::string, std::string>>> tt;
        for (const auto& [k, v] : terrainConfusion)
            tt.push_back({v, k});
        std::sort(tt.rbegin(), tt.rend());
        for (size_t i = 0; i < std::min<size_t>(15, tt.size()); i++)
            std::printf("  %9llu  %s -> %s\n", static_cast<unsigned long long>(tt[i].first), tt[i].second.first.c_str() + 10,
                        tt[i].second.second.c_str() + 10);
    }
    std::printf("biomes: %.2f%% of quarts the same\n", pct(quartsSame, quarts));
    {
        std::vector<std::pair<uint64_t, std::pair<std::string, std::string>>> bt;
        for (const auto& [k, v] : biomeConfusion)
            bt.push_back({v, k});
        std::sort(bt.rbegin(), bt.rend());
        for (size_t i = 0; i < std::min<size_t>(10, bt.size()); i++)
            std::printf("  %9llu  %s -> %s\n", static_cast<unsigned long long>(bt[i].first), bt[i].second.first.c_str(),
                        bt[i].second.second.c_str());
    }
    std::printf("block counts, reference vs ours (largest gaps first):\n");
    {
        std::vector<std::pair<int64_t, std::string>> gaps;
        for (const auto& [k, v] : blocksOurs)
            gaps.push_back({std::llabs(static_cast<long long>(v.first) - static_cast<long long>(v.second)), k});
        std::sort(gaps.rbegin(), gaps.rend());
        for (size_t i = 0; i < std::min<size_t>(25, gaps.size()); i++) {
            const auto& v = blocksOurs[gaps[i].second];
            std::printf("  %-28s %9llu %9llu\n", gaps[i].second.c_str() + 10, static_cast<unsigned long long>(v.first),
                        static_cast<unsigned long long>(v.second));
        }
    }
    std::printf("agreement by reference block (most common first):\n");
    std::vector<std::pair<uint64_t, std::string>> blocks;
    for (const auto& [k, v] : perBlock)
        blocks.push_back({v.first, k});
    std::sort(blocks.rbegin(), blocks.rend());
    for (size_t i = 0; i < std::min<size_t>(25, blocks.size()); i++) {
        const auto& [count, matched] = perBlock[blocks[i].second];
        std::printf("  %-28s %9llu  %6.2f%%\n", blocks[i].second.c_str() + 10, static_cast<unsigned long long>(count),
                    pct(matched, count));
    }
    return 0;
}
