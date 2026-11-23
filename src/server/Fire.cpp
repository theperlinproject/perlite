// Fire: lit by flint and steel or a fire charge, it burns the flammable blocks around it, spreads,
// and burns out when nothing feeds it. Every fire block gets a scheduled tick every 30 to 40 game
// ticks, as in vanilla; netherrack, magma and soul soil keep their fire forever.
#include "server/Server.hpp"

#include "network/session/Session.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/block/Waterlog.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <array>
#include <atomic>
#include <random>
#include <string>
#include <string_view>

namespace perlite {
    namespace {
        struct Flammability {
            int8_t encourage = 0; // how readily fire spreads to the air next to it
            int8_t burn = 0;      // how readily the block itself burns away
        };

        Flammability Classify(const std::string& full) {
            const std::string_view n = std::string_view(full).substr(full.find(':') + 1);
            const auto has = [&](std::string_view p) { return n.find(p) != std::string_view::npos; };
            if (has("crimson") || has("warped"))
                return {};
            if (n == "tnt")
                return {15, 100};
            const bool wood = has("oak") || has("spruce") || has("birch") || has("jungle") || has("acacia") || has("mangrove") ||
                              has("cherry") || has("bamboo");
            if (n.ends_with("_planks") || (n.ends_with("_slab") && wood) || (n.ends_with("_stairs") && wood) ||
                ((n.ends_with("_fence") || n.ends_with("_fence_gate")) && !has("nether")))
                return {5, 20};
            if (n.ends_with("_log") || n.ends_with("_wood") || n == "bamboo_block" || n.starts_with("stripped_"))
                return {5, 5};
            if (n.ends_with("_leaves") || n.ends_with("_wool") || n == "dried_kelp_block" || n == "azalea" ||
                n == "flowering_azalea")
                return {30, 60};
            if (n == "bookshelf" || n == "chiseled_bookshelf" || n == "lectern")
                return {30, 20};
            if (n.ends_with("_carpet") || n == "hay_block" || n == "target")
                return {60, 20};
            if (n == "short_grass" || n == "tall_grass" || n == "fern" || n == "large_fern" || n == "deadbush" ||
                n == "vine" || n == "glow_lichen" || has("tulip") || n == "poppy" || n == "dandelion" ||
                n == "sweet_berry_bush" || n == "leaf_litter" || n == "pink_petals" || n == "wildflowers" ||
                n == "bush" || n == "firefly_bush")
                return {60, 100};
            if (n == "coal_block" || n == "scaffolding" || n == "beehive" || n == "bee_nest" || n == "composter")
                return {5, 5};
            return {};
        }

        Flammability FlammabilityOf(uint16_t id) {
            static std::array<std::atomic<int16_t>, 0x10000> cache{};
            int16_t v = cache[id].load(std::memory_order_relaxed);
            if (v == 0) {
                const Flammability f = Classify(BlockRegistry::Instance().Name(id));
                v = static_cast<int16_t>(0x4000 | (f.encourage << 7) | f.burn);
                cache[id].store(v, std::memory_order_relaxed);
            }
            return {static_cast<int8_t>((v >> 7) & 0x7F), static_cast<int8_t>(v & 0x7F)};
        }

        bool IsFire(const std::string& name) { return name == "minecraft:fire" || name == "minecraft:soul_fire"; }

        constexpr int kSides[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    }

    void Server::ScheduleFire(int dimension, int x, int y, int z) {
        static std::mt19937 rng{std::random_device{}()};
        if (fireQueue.size() >= 1 << 16)
            return;
        fireQueue.push_back({dimension, x, y, z, static_cast<int>(TickCount()) + 30 + static_cast<int>(rng() % 10)});
    }

    bool Server::LightFire(int dimension, int x, int y, int z) {
        ChunkManager& world = World(dimension);
        const DimensionRange range = RangeOf(dimension);
        if (y < range.minY || y > range.MaxY() || !world.Peek({x >> 4, z >> 4}))
            return false;
        if (world.GetBlock(x, y, z) != BlockRegistry::kAir)
            return false;
        const uint16_t below = world.GetBlock(x, y - 1, z);
        bool canStand = BlockSolid(below);
        for (const auto& s : kSides)
            if (FlammabilityOf(world.GetBlock(x + s[0], y + s[1], z + s[2])).encourage > 0)
                canStand = true;
        if (!canStand)
            return false;
        const std::string belowName = BlockRegistry::Instance().Name(below);
        const bool soul = belowName == "minecraft:soul_sand" || belowName == "minecraft:soul_soil";
        const uint16_t fire = BlockCatalog::Instance().DefaultBlock(soul ? "minecraft:soul_fire" : "minecraft:fire");
        if (fire == BlockRegistry::kAir)
            return false;
        ApplyBlock(dimension, x, y, z, fire);
        ScheduleFire(dimension, x, y, z);
        return true;
    }

    void Server::TickFire() {
        if (fireQueue.empty())
            return;
        static std::mt19937 rng{std::random_device{}()};
        const int now = static_cast<int>(TickCount());
        std::vector<FireTick> due;
        std::erase_if(fireQueue, [&](const FireTick& f) {
            if (f.due > now)
                return false;
            due.push_back(f);
            return true;
        });
        const bool spreads = RuleValue("dofiretick") != 0;
        const auto chance = [&](int oneIn) { return oneIn <= 1 || rng() % static_cast<uint32_t>(oneIn) == 0; };
        for (const FireTick& f : due) {
            ChunkManager& world = World(f.dimension);
            if (!world.Peek({f.x >> 4, f.z >> 4}))
                continue;
            const uint16_t id = world.GetBlock(f.x, f.y, f.z);
            const std::string name = BlockRegistry::Instance().Name(id);
            if (!IsFire(name))
                continue;
            if (!spreads) {
                ScheduleFire(f.dimension, f.x, f.y, f.z);
                continue;
            }
            const std::string below = BlockRegistry::Instance().Name(world.GetBlock(f.x, f.y - 1, f.z));
            const bool forever = below == "minecraft:netherrack" || below == "minecraft:magma" ||
                                 below == "minecraft:soul_sand" || below == "minecraft:soul_soil" ||
                                 (f.dimension == kEnd && below == "minecraft:bedrock");
            if (name == "minecraft:soul_fire") {
                if (!forever)
                    ApplyBlock(f.dimension, f.x, f.y, f.z, BlockRegistry::kAir);
                else
                    ScheduleFire(f.dimension, f.x, f.y, f.z);
                continue;
            }
            int age = 0;
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == "age")
                    if (const int32_t* a = std::get_if<int32_t>(&v))
                        age = *a;
            if (raining && !forever && chance(3)) {
                ApplyBlock(f.dimension, f.x, f.y, f.z, BlockRegistry::kAir);
                continue;
            }
            const int newAge = std::min(15, age + static_cast<int>(rng() % 3) / 2);
            if (newAge != age) {
                StateList states = BlockRegistry::Instance().States(id);
                for (auto& [k, v] : states)
                    if (k == "age")
                        v = static_cast<int32_t>(newAge);
                world.SetBlock(f.x, f.y, f.z, BlockRegistry::Instance().Intern(name, std::move(states)));
            }
            if (!forever) {
                bool fed = false;
                for (const auto& s : kSides)
                    if (FlammabilityOf(world.GetBlock(f.x + s[0], f.y + s[1], f.z + s[2])).encourage > 0)
                        fed = true;
                const bool standing = BlockSolid(world.GetBlock(f.x, f.y - 1, f.z));
                if (!fed && (!standing || age > 3)) {
                    ApplyBlock(f.dimension, f.x, f.y, f.z, BlockRegistry::kAir);
                    continue;
                }
                if (age == 15 && chance(4) && FlammabilityOf(world.GetBlock(f.x, f.y - 1, f.z)).burn == 0) {
                    ApplyBlock(f.dimension, f.x, f.y, f.z, BlockRegistry::kAir);
                    continue;
                }
            }
            // The blocks around burn away, some catching fire themselves.
            for (const auto& s : kSides) {
                const int bx = f.x + s[0], by = f.y + s[1], bz = f.z + s[2];
                if (!world.Peek({bx >> 4, bz >> 4}))
                    continue;
                const uint16_t near = world.GetBlock(bx, by, bz);
                const int burn = FlammabilityOf(near).burn;
                if (burn == 0 || static_cast<int>(rng() % (s[1] != 0 ? 250u : 300u)) >= burn)
                    continue;
                if (BlockRegistry::Instance().Name(near) == "minecraft:tnt") {
                    ApplyBlock(f.dimension, bx, by, bz, BlockRegistry::kAir);
                    MobsIn(f.dimension).SpawnPrimedTnt(bx + 0.5, by, bz + 0.5, 80);
                    continue;
                }
                ApplyBlock(f.dimension, bx, by, bz, BlockRegistry::kAir);
                if (static_cast<int>(rng() % static_cast<uint32_t>(age + 10)) < 5)
                    LightFire(f.dimension, bx, by, bz);
            }
            // Fire jumps to air near flammable blocks.
            for (int attempt = 0; attempt < 3; attempt++) {
                const int bx = f.x + static_cast<int>(rng() % 3) - 1;
                const int by = f.y + static_cast<int>(rng() % 6) - 1;
                const int bz = f.z + static_cast<int>(rng() % 3) - 1;
                if (!world.Peek({bx >> 4, bz >> 4}) || world.GetBlock(bx, by, bz) != BlockRegistry::kAir)
                    continue;
                int encourage = 0;
                for (const auto& s : kSides)
                    encourage = std::max<int>(encourage, FlammabilityOf(world.GetBlock(bx + s[0], by + s[1], bz + s[2])).encourage);
                if (encourage > 0 && static_cast<int>(rng() % 100) < encourage / (by > f.y + 1 ? 2 : 1))
                    LightFire(f.dimension, bx, by, bz);
            }
            ScheduleFire(f.dimension, f.x, f.y, f.z);
        }
    }
}
