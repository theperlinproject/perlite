#include "world/level/block/Waterlog.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/Chunk.hpp"

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace perlite::waterlog {
    namespace {
        enum : int8_t { kUnknown = 0, kNone = 1, kHolds = 2, kNeeds = 3, kWater = 4 };

        int8_t Classify(uint16_t id) {
            const std::string full = BlockRegistry::Instance().Name(id);
            const std::string_view n = std::string_view(full).substr(full.find(':') + 1);
            if (n == "water" || n == "flowing_water")
                return kWater;
            if (n == "kelp" || n == "seagrass")
                return kNeeds;
            const auto has = [&](std::string_view part) { return n.find(part) != std::string_view::npos; };
            const auto ends = [&](std::string_view tail) { return n.ends_with(tail); };
            if (has("double_slab") || has("coral_block"))
                return kNone;
            if (ends("_slab") || ends("_stairs") || ends("_fence") ||
                ends("_fence_gate") || ends("_wall") || ends("_pane") || n == "iron_bars" || ends("chain") ||
                ends("lantern") || n == "ladder" || ends("_sign") || ends("_hanging_sign") || ends("_trapdoor") ||
                n == "scaffolding" || n == "pointed_dripstone" || ends("amethyst_bud") || n == "amethyst_cluster" ||
                n == "glow_lichen" || n == "sculk_vein" || ends("lightning_rod") || ends("candle") || n == "conduit" ||
                n == "chest" || n == "trapped_chest" || n == "ender_chest" || ends("campfire") ||
                ends("copper_grate") || n == "hanging_roots" || n == "mangrove_roots" ||
                n == "mangrove_propagule" || n == "big_dripleaf" || n == "small_dripleaf_block" ||
                n == "heavy_core" || n == "decorated_pot" || n == "sea_pickle" || n == "mangrove_leaves" ||
                (has("coral") && !has("block")) || n == "barrier" || n == "light_block" || ends("_shelf") ||
                n == "copper_bars" || ends("_copper_bars") || n == "dried_ghast")
                return kHolds;
            return kNone;
        }

        int8_t KindOf(uint16_t id) {
            static std::array<std::atomic<int8_t>, 0x10000> kinds{};
            int8_t k = kinds[id].load(std::memory_order_relaxed);
            if (k == kUnknown) {
                k = Classify(id);
                kinds[id].store(k, std::memory_order_relaxed);
            }
            return k;
        }
    }

    bool IsWater(uint16_t id) { return id != BlockRegistry::kAir && KindOf(id) == kWater; }
    bool Holds(uint16_t id) {
        const int8_t k = KindOf(id);
        return k == kHolds || k == kNeeds;
    }
    bool NeedsWater(uint16_t id) { return KindOf(id) == kNeeds; }

    uint16_t Water() {
        static const uint16_t water = BlockCatalog::Instance().DefaultBlock("minecraft:water");
        return water;
    }

    void FillGenerated(ChunkColumn& column) {
        const uint16_t water = Water();
        if (water == BlockRegistry::kAir)
            return;
        for (int section = 0; section < kSections; section++) {
            const ChunkColumn::Section& s = column.SectionAt(section);
            if (!s.cells)
                continue;
            const auto& cells = *s.cells;
            const int baseY = kMinY + section * 16;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 16; y++) {
                        const uint16_t id = cells[static_cast<size_t>(CellIndex(x, y, z))];
                        if (id == BlockRegistry::kAir)
                            continue;
                        const int8_t k = KindOf(id);
                        if (k != kHolds && k != kNeeds)
                            continue;
                        const int wy = baseY + y;
                        bool wet = k == kNeeds;
                        if (!wet) {
                            // Water beside or above: inside the column only, as the generator saw it.
                            static constexpr int kNear[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
                            for (const auto& d : kNear) {
                                const int nx = x + d[0], nz = z + d[2];
                                if (nx < 0 || nx > 15 || nz < 0 || nz > 15)
                                    continue;
                                if (IsWater(column.Get(nx, wy + d[1], nz))) {
                                    wet = true;
                                    break;
                                }
                            }
                        }
                        if (wet)
                            column.SetLiquid(x, wy, z, water);
                    }
        }
    }
}
