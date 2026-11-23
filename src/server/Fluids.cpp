// Water and lava flow. A fluid block is ticked a while after something next to it changes (water
// every 5 game ticks, lava every 30, or 10 in the nether), the way vanilla schedules fluid updates:
// it settles its own level from its neighbours, falls, and spreads sideways towards the nearest
// drop. Two water sources make a third, lava meeting water turns to obsidian, cobblestone or stone,
// and water held by a waterlogged block spreads like a source. Buckets fill and empty.
#include "server/Server.hpp"

#include "network/session/Session.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/block/Waterlog.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>

namespace perlite {
    namespace {
        enum : int8_t { kNone = 0, kWater = 1, kLava = 2 };
        constexpr int kSides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

        struct FluidInfo {
            int8_t kind = kNone;
            int8_t depth = 0; // 0 source, 1..7 flowing, 8+ falling
        };

        // Fluid kind and depth per block id, worked out once.
        FluidInfo FluidOf(uint16_t id) {
            static std::array<std::atomic<int16_t>, 0x10000> cache{};
            if (id == BlockRegistry::kAir)
                return {};
            int16_t v = cache[id].load(std::memory_order_relaxed);
            if (v == 0) {
                const std::string name = BlockRegistry::Instance().Name(id);
                int8_t kind = kNone;
                if (name == "minecraft:water" || name == "minecraft:flowing_water")
                    kind = kWater;
                else if (name == "minecraft:lava" || name == "minecraft:flowing_lava")
                    kind = kLava;
                int depth = 0;
                if (kind != kNone)
                    for (const auto& [k, value] : BlockRegistry::Instance().States(id))
                        if (k == "liquid_depth")
                            if (const int32_t* d = std::get_if<int32_t>(&value))
                                depth = *d;
                v = static_cast<int16_t>(0x4000 | (kind << 8) | (depth & 0xFF));
                cache[id].store(v, std::memory_order_relaxed);
            }
            return {static_cast<int8_t>((v >> 8) & 0x3F), static_cast<int8_t>(v & 0xFF)};
        }

        // The block for a fluid at a depth: sources are still, the rest flowing.
        uint16_t FluidBlock(int kind, int depth) {
            static std::array<std::atomic<uint16_t>, 2 * 16> cache{};
            const size_t slot = static_cast<size_t>((kind - 1) * 16 + std::clamp(depth, 0, 15));
            if (uint16_t id = cache[slot].load(std::memory_order_relaxed))
                return id;
            const std::string name = depth == 0 ? (kind == kWater ? "minecraft:water" : "minecraft:lava")
                                                : (kind == kWater ? "minecraft:flowing_water" : "minecraft:flowing_lava");
            const uint16_t id = BlockRegistry::Instance().Intern(name, {{"liquid_depth", static_cast<int32_t>(depth)}});
            cache[slot].store(id, std::memory_order_relaxed);
            return id;
        }

        // Blocks a fluid washes away: air and the small plants, torches and the like that have no
        // collision box. Doors, signs, ladders, portals and blocks that can hold water stop it.
        bool Washable(uint16_t id) {
            static std::array<std::atomic<int8_t>, 0x10000> cache{};
            if (id == BlockRegistry::kAir)
                return true;
            int8_t v = cache[id].load(std::memory_order_relaxed);
            if (v == 0) {
                const std::string full = BlockRegistry::Instance().Name(id);
                const std::string_view n = std::string_view(full).substr(full.find(':') + 1);
                bool ok = !BlockSolid(id) && !BlockLiquid(id) && !waterlog::Holds(id);
                for (std::string_view stop : {"portal", "door", "sign", "ladder", "banner", "lever", "button",
                                              "pressure_plate", "bed", "cake", "scaffolding", "end_gateway",
                                              "structure_void", "barrier", "web"})
                    if (n.find(stop) != std::string_view::npos)
                        ok = false;
                v = ok ? 1 : 2;
                cache[id].store(v, std::memory_order_relaxed);
            }
            return v == 1;
        }

        uint64_t FluidKey(int dimension, int x, int y, int z) {
            return (static_cast<uint64_t>(dimension & 3) << 62) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(y + 512) & 0x3FF) << 52) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0x3FFFFFF) << 26) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(z) & 0x3FFFFFF));
        }
    }

    int Server::FluidDelay(int dimension, uint16_t block) const {
        const FluidInfo f = FluidOf(block);
        if (f.kind == kLava)
            return dimension == kNether ? 10 : 30;
        return 5;
    }

    void Server::ScheduleFluid(int dimension, int x, int y, int z, int delay) {
        if (fluidQueue.size() >= (1u << 20))
            return;
        if (!fluidPending.insert(FluidKey(dimension, x, y, z)).second)
            return;
        fluidQueue.push_back({dimension, x, y, z, static_cast<int>(TickCount()) + std::max(1, delay)});
    }

    // Called for every block change: the fluids at and around it get a tick.
    void Server::FluidNeighbourChanged(int dimension, int x, int y, int z) {
        ChunkManager& world = World(dimension);
        static constexpr int kAround[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const auto& o : kAround) {
            const int bx = x + o[0], by = y + o[1], bz = z + o[2];
            if (!world.Peek({bx >> 4, bz >> 4}))
                continue;
            const uint16_t id = world.GetBlock(bx, by, bz);
            if (FluidOf(id).kind != kNone)
                ScheduleFluid(dimension, bx, by, bz, FluidDelay(dimension, id));
            else if (id != BlockRegistry::kAir && waterlog::Holds(id) && world.GetLiquid(bx, by, bz) != BlockRegistry::kAir)
                ScheduleFluid(dimension, bx, by, bz, 5);
        }
    }

    void Server::TickFluids() {
        if (fluidQueue.empty())
            return;
        const int now = static_cast<int>(TickCount());
        std::vector<FluidTick> due;
        // At most this many fluid blocks a tick; the rest wait for the next one, so a flood can
        // never stall the server.
        constexpr size_t kBudget = 8192;
        std::erase_if(fluidQueue, [&](const FluidTick& f) {
            if (f.due > now || due.size() >= kBudget)
                return false;
            due.push_back(f);
            return true;
        });
        for (const FluidTick& f : due)
            fluidPending.erase(FluidKey(f.dimension, f.x, f.y, f.z));
        for (const FluidTick& f : due)
            FlowAt(f.dimension, f.x, f.y, f.z);
    }

    void Server::FlowAt(int dimension, int x, int y, int z) {
        ChunkManager& world = World(dimension);
        const DimensionRange range = RangeOf(dimension);
        if (y < range.minY || y > range.MaxY() || !world.Peek({x >> 4, z >> 4}))
            return;
        const auto loaded = [&](int bx, int bz) { return static_cast<bool>(world.Peek({bx >> 4, bz >> 4})); };
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < range.minY || by > range.MaxY() || !loaded(bx, bz))
                return BlockCatalog::Instance().DefaultBlock("minecraft:bedrock");
            return world.GetBlock(bx, by, bz);
        };
        // The fluid at a spot, counting the water held by a waterlogged block as a source.
        const auto fluidAt = [&](int bx, int by, int bz) -> FluidInfo {
            const uint16_t id = blockAt(bx, by, bz);
            const FluidInfo f = FluidOf(id);
            if (f.kind != kNone)
                return f;
            if (id != BlockRegistry::kAir && waterlog::Holds(id) && loaded(bx, bz) &&
                world.GetLiquid(bx, by, bz) != BlockRegistry::kAir)
                return {kWater, 0};
            return {};
        };

        const uint16_t here = world.GetBlock(x, y, z);
        FluidInfo self = fluidAt(x, y, z);
        if (self.kind == kNone)
            return;
        const bool logged = FluidOf(here).kind == kNone; // water in a waterlogged block
        const int kind = self.kind;
        const bool nether = dimension == kNether;
        const int drop = kind == kLava && !nether ? 2 : 1;
        const int slopeReach = kind == kLava && !nether ? 2 : 4;

        // Lava that touches water hardens: a source to obsidian, flowing lava to cobblestone.
        if (kind == kLava) {
            static constexpr int kTouch[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const auto& o : kTouch)
                if (fluidAt(x + o[0], y + o[1], z + o[2]).kind == kWater) {
                    const char* into = self.depth == 0 ? "minecraft:obsidian" : "minecraft:cobblestone";
                    ApplyBlock(dimension, x, y, z, BlockCatalog::Instance().DefaultBlock(into));
                    for (Session* s : PlayersIn(dimension))
                        s->PlaySound("Fizz", x + 0.5, y + 0.5, z + 0.5);
                    return;
                }
        }

        // A flowing block takes its level from its neighbours.
        if (!logged && self.depth != 0) {
            int best = 99, sources = 0;
            for (const auto& sd : kSides) {
                const FluidInfo n = fluidAt(x + sd[0], y, z + sd[1]);
                if (n.kind != kind)
                    continue;
                const int d = n.depth >= 8 ? 0 : n.depth;
                if (n.depth == 0)
                    sources++;
                best = std::min(best, d);
            }
            int next;
            const FluidInfo above = fluidAt(x, y + 1, z);
            const FluidInfo belowFluid = fluidAt(x, y - 1, z);
            const uint16_t belowId = blockAt(x, y - 1, z);
            if (kind == kWater && sources >= 2 && (BlockSolid(belowId) || (belowFluid.kind == kWater && belowFluid.depth == 0)))
                next = 0;
            else if (above.kind == kind)
                next = 8;
            else if (best == 99 || best + drop > 7)
                next = -1;
            else
                next = best + drop;
            if (next != self.depth) {
                ApplyBlock(dimension, x, y, z, next < 0 ? BlockRegistry::kAir : FluidBlock(kind, next));
                if (next < 0)
                    return;
                self.depth = static_cast<int8_t>(next);
            }
        }

        const bool falling = self.depth >= 8;
        const bool source = self.depth == 0;

        // Down first.
        bool wentDown = false;
        if (y - 1 >= range.minY) {
            const uint16_t below = blockAt(x, y - 1, z);
            const FluidInfo b = FluidOf(below);
            if (kind == kLava && b.kind == kWater) {
                ApplyBlock(dimension, x, y - 1, z, BlockCatalog::Instance().DefaultBlock("minecraft:stone"));
                for (Session* s : PlayersIn(dimension))
                    s->PlaySound("Fizz", x + 0.5, y - 0.5, z + 0.5);
                wentDown = true;
            } else if (b.kind == kind) {
                if (b.depth != 0 && b.depth < 8)
                    ApplyBlock(dimension, x, y - 1, z, FluidBlock(kind, 8));
                wentDown = b.depth != 0;
            } else if (b.kind == kNone && Washable(below)) {
                if (below != BlockRegistry::kAir)
                    DropBlockLoot(dimension, x, y - 1, z, below, std::string(), 1.0f, nullptr, 0, 0);
                ApplyBlock(dimension, x, y - 1, z, FluidBlock(kind, 8));
                wentDown = true;
            }
        }
        if (wentDown && !source)
            return;

        // Then sideways, towards the nearest drop within reach.
        const int sideDepth = falling ? drop : self.depth + drop;
        if (sideDepth > 7)
            return;
        const auto open = [&](int bx, int by, int bz) {
            const uint16_t id = blockAt(bx, by, bz);
            const FluidInfo n = FluidOf(id);
            if (n.kind != kNone)
                return n.kind == kind && n.depth != 0;
            return Washable(id);
        };
        const auto hole = [&](int bx, int by, int bz) {
            const uint16_t id = blockAt(bx, by - 1, bz);
            const FluidInfo n = FluidOf(id);
            return (n.kind == kNone && Washable(id)) || (n.kind == kind && n.depth != 0);
        };
        // Steps to the nearest drop going out in one direction (vanilla's slope search).
        std::function<int(int, int, int, int)> slope = [&](int bx, int bz, int distance, int from) -> int {
            int best = 1000;
            for (int d = 0; d < 4; d++) {
                if (d == from)
                    continue;
                const int nx = bx + kSides[d][0], nz = bz + kSides[d][1];
                if (!open(nx, y, nz) || (FluidOf(blockAt(nx, y, nz)).kind == kind && FluidOf(blockAt(nx, y, nz)).depth == 0))
                    continue;
                if (hole(nx, y, nz))
                    return distance;
                if (distance < slopeReach)
                    best = std::min(best, slope(nx, nz, distance + 1, d ^ 1));
            }
            return best;
        };
        std::array<int, 4> cost{};
        int cheapest = 1000;
        for (int d = 0; d < 4; d++) {
            const int nx = x + kSides[d][0], nz = z + kSides[d][1];
            if (!open(nx, y, nz)) {
                cost[d] = 2000;
                continue;
            }
            cost[d] = hole(nx, y, nz) ? 0 : slope(nx, nz, 1, d ^ 1);
            cheapest = std::min(cheapest, cost[d]);
        }
        for (int d = 0; d < 4; d++) {
            if (cost[d] >= 2000 || cost[d] != cheapest)
                continue;
            const int nx = x + kSides[d][0], nz = z + kSides[d][1];
            const uint16_t id = blockAt(nx, y, nz);
            const FluidInfo n = FluidOf(id);
            if (n.kind == kind && (n.depth >= 8 || n.depth <= sideDepth))
                continue;
            if (n.kind == kNone && id != BlockRegistry::kAir)
                DropBlockLoot(dimension, nx, y, nz, id, std::string(), 1.0f, nullptr, 0, 0);
            ApplyBlock(dimension, nx, y, nz, FluidBlock(kind, sideDepth));
        }
    }

    // Empty buckets take a source block the player looks at; full ones pour it out.
    bool Server::UseBucket(Session& by, const std::string& item, bool clicked, int x, int y, int z, int face) {
        const int dim = by.DimensionId();
        ChunkManager& world = World(dim);
        const DimensionRange range = RangeOf(dim);
        const auto takeHeld = [&](const char* into) {
            if (by.Creative())
                return;
            ItemStack& held = by.Inventory().HeldRef();
            const CatalogItem* next = BlockCatalog::Instance().ItemByName(into);
            if (!next)
                return;
            if (held.count <= 1) {
                held = by.Inventory().MakeStack(next->networkId, 1);
            } else {
                held.count = static_cast<uint16_t>(held.count - 1);
                held.stackId = by.Inventory().NewStackId();
                if (by.Inventory().Give(next->networkId, 1) > 0) {
                    ItemStack spill = by.Inventory().MakeStack(next->networkId, 1);
                    DropFromPlayer(by, spill);
                }
            }
            by.SendInventory();
        };
        const auto sound = [&](const char* name, int bx, int by_, int bz) {
            for (Session* s : PlayersIn(dim))
                s->PlaySound(name, bx + 0.5, by_ + 0.5, bz + 0.5);
        };

        if (item == "minecraft:bucket") {
            // Walk the look ray to the first source block, stopping at anything solid.
            const double yawRad = static_cast<double>(by.Yaw()) * 3.14159265358979323846 / 180.0;
            const double pitchRad = static_cast<double>(by.Pitch()) * 3.14159265358979323846 / 180.0;
            const double dx = -std::sin(yawRad) * std::cos(pitchRad), dy = -std::sin(pitchRad),
                         dz = std::cos(yawRad) * std::cos(pitchRad);
            for (double t = 0.0; t <= 5.5; t += 0.05) {
                const int bx = static_cast<int>(std::floor(by.X() + dx * t)), byy = static_cast<int>(std::floor(by.Y() + dy * t)),
                          bz = static_cast<int>(std::floor(by.Z() + dz * t));
                if (byy < range.minY || byy > range.MaxY() || !world.Peek({bx >> 4, bz >> 4}))
                    return false;
                const uint16_t id = world.GetBlock(bx, byy, bz);
                const FluidInfo f = FluidOf(id);
                if (f.kind != kNone && f.depth == 0) {
                    ApplyBlock(dim, bx, byy, bz, BlockRegistry::kAir);
                    sound(f.kind == kWater ? "BucketFillWater" : "BucketFillLava", bx, byy, bz);
                    takeHeld(f.kind == kWater ? "minecraft:water_bucket" : "minecraft:lava_bucket");
                    return true;
                }
                if (id != BlockRegistry::kAir && waterlog::Holds(id) && world.GetLiquid(bx, byy, bz) != BlockRegistry::kAir) {
                    SetLiquidAt(dim, bx, byy, bz, BlockRegistry::kAir);
                    FluidNeighbourChanged(dim, bx, byy, bz);
                    sound("BucketFillWater", bx, byy, bz);
                    takeHeld("minecraft:water_bucket");
                    return true;
                }
                if (f.kind == kNone && BlockSolid(id))
                    return false;
            }
            return false;
        }

        const bool lava = item == "minecraft:lava_bucket";
        const bool water = !lava;
        if (!clicked || face < 0 || face > 5)
            return false;
        static constexpr int kOffset[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        // Water poured on a block that can hold it waterlogs that block.
        const uint16_t target = world.GetBlock(x, y, z);
        if (water && target != BlockRegistry::kAir && waterlog::Holds(target) && world.GetLiquid(x, y, z) == BlockRegistry::kAir &&
            dim != kNether) {
            SetLiquidAt(dim, x, y, z, waterlog::Water());
            FluidNeighbourChanged(dim, x, y, z);
        } else {
            const int tx = x + kOffset[face][0], ty = y + kOffset[face][1], tz = z + kOffset[face][2];
            if (ty < range.minY || ty > range.MaxY() || !world.Peek({tx >> 4, tz >> 4}) || !by.InReach(tx, ty, tz))
                return false;
            const uint16_t there = world.GetBlock(tx, ty, tz);
            const FluidInfo f = FluidOf(there);
            if (!(f.kind != kNone || Washable(there)))
                return false;
            if (water && dim == kNether) {
                sound("Fizz", tx, ty, tz); // water boils away in the nether
            } else {
                ApplyBlock(dim, tx, ty, tz, FluidBlock(lava ? kLava : kWater, 0));
                sound(lava ? "BucketEmptyLava" : "BucketEmptyWater", tx, ty, tz);
            }
            // A bucket of fish lets the fish go.
            if (item.ends_with("_bucket") && item != "minecraft:water_bucket" && item != "minecraft:lava_bucket" && dim == kOverworld) {
                std::string mob = item.substr(0, item.size() - 7);
                if (mob == "minecraft:tropical_fish")
                    mob = "minecraft:tropicalfish";
                Mobs().Spawn(mob, tx + 0.5, ty, tz + 0.5, true);
            }
        }
        takeHeld("minecraft:bucket");
        return true;
    }
}
