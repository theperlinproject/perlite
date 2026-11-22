// Random block ticks: each tick, a few random blocks in every section of the chunks around players get
// a chance to change by themselves (crops grow, saplings become trees, grass spreads, leaves decay,
// copper ages). The "randomtickspeed" game rule sets how many blocks per section are picked.
#include "server/Server.hpp"

#include "network/session/Session.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <unordered_set>

namespace perlite {
    namespace {
        enum class Kind : uint8_t {
            Unknown, None, Crop, Stem, Farmland, Sapling, Grass, Mycelium, Reeds, Cactus, Leaves, Berry,
            Wart, Cocoa, Copper, Budding, Kelp
        };

        std::string Bare(const std::string& name) {
            const auto colon = name.find(':');
            return colon == std::string::npos ? name : name.substr(colon + 1);
        }

        Kind Classify(const std::string& full) {
            const std::string n = Bare(full);
            if (n == "wheat" || n == "carrots" || n == "potatoes" || n == "beetroot" || n == "torchflower_crop")
                return Kind::Crop;
            if (n == "pumpkin_stem" || n == "melon_stem")
                return Kind::Stem;
            if (n == "farmland")
                return Kind::Farmland;
            if (n.ends_with("_sapling") && n != "bamboo_sapling")
                return Kind::Sapling;
            if (n == "grass_block")
                return Kind::Grass;
            if (n == "mycelium")
                return Kind::Mycelium;
            if (n == "reeds")
                return Kind::Reeds;
            if (n == "cactus")
                return Kind::Cactus;
            if (n.ends_with("leaves"))
                return Kind::Leaves;
            if (n == "sweet_berry_bush")
                return Kind::Berry;
            if (n == "nether_wart")
                return Kind::Wart;
            if (n == "cocoa")
                return Kind::Cocoa;
            if (n == "budding_amethyst")
                return Kind::Budding;
            if (n == "kelp")
                return Kind::Kelp;
            if (n.contains("copper") && !n.starts_with("waxed_") && !n.starts_with("oxidized_") &&
                !n.contains("raw_") && !n.contains("_ore") && n != "copper_torch" && !n.contains("golem"))
                return Kind::Copper;
            return Kind::None;
        }

        // The kind of each block id, worked out once per id (block ids only grow).
        Kind KindOf(uint16_t id) {
            static std::vector<Kind> kinds;
            if (id >= kinds.size())
                kinds.resize(static_cast<size_t>(id) + 256, Kind::Unknown);
            if (kinds[id] == Kind::Unknown)
                kinds[id] = Classify(BlockRegistry::Instance().Name(id));
            return kinds[id];
        }

        const StateValue* State(const StateList& states, const std::string& key) {
            for (const auto& [k, v] : states)
                if (k == key)
                    return &v;
            return nullptr;
        }

        int IntState(uint16_t id, const std::string& key, int fallback = 0) {
            const StateList states = BlockRegistry::Instance().States(id);
            const StateValue* v = State(states, key);
            if (!v)
                return fallback;
            if (const int32_t* i = std::get_if<int32_t>(v))
                return *i;
            if (const bool* b = std::get_if<bool>(v))
                return *b ? 1 : 0;
            return fallback;
        }

        uint16_t With(uint16_t id, const std::string& key, StateValue value) {
            StateList states = BlockRegistry::Instance().States(id);
            for (auto& [k, v] : states)
                if (k == key)
                    v = std::move(value);
            return BlockRegistry::Instance().Intern(BlockRegistry::Instance().Name(id), std::move(states));
        }

        uint16_t Block(const std::string& name) {
            return BlockCatalog::Instance().DefaultBlock(name);
        }

        bool IsAir(const std::string& n) {
            return n == "minecraft:air" || n.empty();
        }

        bool IsLog(const std::string& n) {
            return n.ends_with("_log") || n.ends_with("_wood") ||
                   (n.ends_with("_stem") && !n.contains("pumpkin") && !n.contains("melon")) ||
                   n == "minecraft:mangrove_roots";
        }

        bool IsWater(const std::string& n) {
            return n == "minecraft:water" || n == "minecraft:flowing_water";
        }

        // Blocks that let light and air through, so grass under them survives and can spread.
        bool LetsLightThrough(const std::string& n) {
            if (IsAir(n))
                return true;
            static const char* parts[] = {"glass", "leaves", "flower", "sapling", "torch", "short_grass", "tall_grass",
                                          "fern", "_slab", "_stairs", "fence", "_wall", "sign", "button", "rail",
                                          "carpet", "snow_layer", "pressure_plate", "lantern", "vine", "mushroom",
                                          "rose", "tulip", "dandelion", "poppy", "orchid", "allium", "bluet",
                                          "daisy", "lily", "bush", "reeds", "ladder", "door", "chain", "bars"};
            for (const char* p : parts)
                if (n.contains(p))
                    return true;
            return false;
        }

        std::string NextCopperStage(const std::string& full) {
            const std::string n = Bare(full);
            std::string next;
            if (n.starts_with("weathered_"))
                next = "oxidized_" + n.substr(10);
            else if (n.starts_with("exposed_"))
                next = "weathered_" + n.substr(8);
            else if (n == "copper_block")
                next = "exposed_copper";
            else
                next = "exposed_" + n;
            return BlockCatalog::Instance().Knows("minecraft:" + next) ? "minecraft:" + next : std::string();
        }

        constexpr std::array<std::array<int, 3>, 4> kSides{{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}};
    }

    void Server::TickRandom() {
        const int speed = RuleValue("randomtickspeed");
        if (speed <= 0)
            return;
        static std::mt19937 rng{std::random_device{}()};
        const int radius = static_cast<int>(std::clamp<uint32_t>(config.tickDistance, 1, 12));
        for (int dimension : {kOverworld, kNether, kEnd}) {
            const std::vector<Session*> players = PlayersIn(dimension);
            if (players.empty())
                continue;
            std::set<std::pair<int, int>> columns;
            for (const Session* s : players) {
                const int cx = static_cast<int>(std::floor(s->X())) >> 4;
                const int cz = static_cast<int>(std::floor(s->Z())) >> 4;
                for (int dx = -radius; dx <= radius; dx++)
                    for (int dz = -radius; dz <= radius; dz++)
                        columns.insert({cx + dx, cz + dz});
            }
            ChunkManager& world = World(dimension);
            const DimensionRange range = RangeOf(dimension);
            for (const auto& [cx, cz] : columns) {
                if (!world.Peek({cx, cz}))
                    continue;
                for (int section = 0; section < range.SectionCount(); section++)
                    for (int i = 0; i < speed; i++) {
                        const uint32_t r = rng();
                        const int x = (cx << 4) | static_cast<int>(r & 15);
                        const int z = (cz << 4) | static_cast<int>((r >> 4) & 15);
                        const int y = range.minY + section * 16 + static_cast<int>((r >> 8) & 15);
                        const uint16_t id = world.GetBlock(x, y, z);
                        if (id != BlockRegistry::kAir && KindOf(id) != Kind::None)
                            RandomTickBlock(dimension, x, y, z, rng);
                    }
            }
        }
    }

    void Server::RandomTickBlock(int dimension, int x, int y, int z, std::mt19937& rng) {
        ChunkManager& world = World(dimension);
        const uint16_t id = world.GetBlock(x, y, z);
        const auto name = [&](int bx, int by, int bz) {
            const DimensionRange range = RangeOf(dimension);
            if (by < range.minY || by > range.MaxY() || !world.Peek({bx >> 4, bz >> 4}))
                return std::string("minecraft:bedrock");
            return BlockRegistry::Instance().Name(world.GetBlock(bx, by, bz));
        };
        const auto chance = [&](int oneIn) { return oneIn <= 1 || rng() % static_cast<uint32_t>(oneIn) == 0; };
        const DimensionRange range = RangeOf(dimension);

        switch (KindOf(id)) {
        case Kind::Crop: {
            const int growth = IntState(id, "growth");
            if (growth >= 7)
                return;
            const std::string below = name(x, y - 1, z);
            if (below != "minecraft:farmland")
                return;
            const bool moist = IntState(world.GetBlock(x, y - 1, z), "moisturized_amount") > 0;
            if (!chance(moist ? 3 : 6))
                return;
            ApplyBlock(dimension, x, y, z, With(id, "growth", static_cast<int32_t>(growth + 1)));
            return;
        }
        case Kind::Stem: {
            const int growth = IntState(id, "growth");
            if (!chance(3))
                return;
            if (growth < 7) {
                ApplyBlock(dimension, x, y, z, With(id, "growth", static_cast<int32_t>(growth + 1)));
                return;
            }
            const bool pumpkin = BlockRegistry::Instance().Name(id) == "minecraft:pumpkin_stem";
            const std::string fruit = pumpkin ? "minecraft:pumpkin" : "minecraft:melon_block";
            for (const auto& side : kSides)
                if (name(x + side[0], y, z + side[2]) == fruit)
                    return;
            const auto& side = kSides[rng() % 4];
            const int fx = x + side[0], fz = z + side[2];
            const std::string ground = name(fx, y - 1, fz);
            if (!IsAir(name(fx, y, fz)) ||
                !(ground == "minecraft:dirt" || ground == "minecraft:grass_block" || ground == "minecraft:farmland" ||
                  ground == "minecraft:podzol" || ground == "minecraft:coarse_dirt" || ground == "minecraft:moss_block"))
                return;
            ApplyBlock(dimension, fx, y, fz, Block(fruit));
            // facing_direction: 2 north (z-), 3 south (z+), 4 west (x-), 5 east (x+)
            const int facing = side[2] < 0 ? 2 : side[2] > 0 ? 3 : side[0] < 0 ? 4 : 5;
            ApplyBlock(dimension, x, y, z, With(id, "facing_direction", static_cast<int32_t>(facing)));
            return;
        }
        case Kind::Farmland: {
            bool water = false;
            for (int dx = -4; dx <= 4 && !water; dx++)
                for (int dz = -4; dz <= 4 && !water; dz++)
                    for (int dy = 0; dy <= 1 && !water; dy++)
                        water = IsWater(name(x + dx, y + dy, z + dz));
            const bool wet = raining && dimension == kOverworld;
            const int moisture = IntState(id, "moisturized_amount");
            if (water || wet) {
                if (moisture < 7)
                    ApplyBlock(dimension, x, y, z, With(id, "moisturized_amount", static_cast<int32_t>(7)));
            } else if (moisture > 0) {
                ApplyBlock(dimension, x, y, z, With(id, "moisturized_amount", static_cast<int32_t>(moisture - 1)));
            } else {
                const uint16_t above = world.GetBlock(x, y + 1, z);
                if (KindOf(above) != Kind::Crop && KindOf(above) != Kind::Stem)
                    ApplyBlock(dimension, x, y, z, Block("minecraft:dirt"));
            }
            return;
        }
        case Kind::Sapling: {
            if (!chance(7))
                return;
            if (IntState(id, "age_bit") == 0) {
                ApplyBlock(dimension, x, y, z, With(id, "age_bit", true));
                return;
            }
            std::string kind = Bare(BlockRegistry::Instance().Name(id));
            kind = kind.substr(0, kind.size() - std::string("_sapling").size());
            GrowTree(dimension, x, y, z, kind, rng);
            return;
        }
        case Kind::Grass:
        case Kind::Mycelium: {
            const bool grass = KindOf(id) == Kind::Grass;
            if (!LetsLightThrough(name(x, y + 1, z))) {
                if (chance(4))
                    ApplyBlock(dimension, x, y, z, Block("minecraft:dirt"));
                return;
            }
            for (int attempt = 0; attempt < 4; attempt++) {
                const int tx = x + static_cast<int>(rng() % 3) - 1;
                const int ty = y + static_cast<int>(rng() % 5) - 3;
                const int tz = z + static_cast<int>(rng() % 3) - 1;
                if (name(tx, ty, tz) != "minecraft:dirt" || !LetsLightThrough(name(tx, ty + 1, tz)) ||
                    IsWater(name(tx, ty + 1, tz)))
                    continue;
                ApplyBlock(dimension, tx, ty, tz, Block(grass ? "minecraft:grass_block" : "minecraft:mycelium"));
            }
            return;
        }
        case Kind::Reeds:
        case Kind::Cactus: {
            if (!IsAir(name(x, y + 1, z)) || y + 1 > range.MaxY())
                return;
            const std::string self = BlockRegistry::Instance().Name(id);
            int height = 1;
            while (height < 3 && name(x, y - height, z) == self)
                height++;
            if (height >= 3)
                return;
            const int age = IntState(id, "age");
            if (age < 15) {
                ApplyBlock(dimension, x, y, z, With(id, "age", static_cast<int32_t>(age + 1)));
                return;
            }
            if (KindOf(id) == Kind::Cactus)
                for (const auto& side : kSides)
                    if (!IsAir(name(x + side[0], y + 1, z + side[2])))
                        return;
            ApplyBlock(dimension, x, y + 1, z, Block(self));
            ApplyBlock(dimension, x, y, z, With(id, "age", static_cast<int32_t>(0)));
            return;
        }
        case Kind::Leaves: {
            if (IntState(id, "persistent_bit") != 0)
                return;
            // Leaves stay while a log is within 6 steps through other leaves.
            std::deque<std::array<int, 4>> open{{x, y, z, 0}};
            std::unordered_set<int64_t> seen;
            const auto key = [](int a, int b, int c) {
                return (static_cast<int64_t>(a & 0xFFFFF) << 40) | (static_cast<int64_t>(b & 0xFFFFF) << 20) |
                       static_cast<int64_t>(c & 0xFFFFF);
            };
            seen.insert(key(x, y, z));
            static constexpr int kDirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            while (!open.empty() && seen.size() < 1024) {
                const auto [cx, cy, cz, d] = open.front();
                open.pop_front();
                for (const auto& dir : kDirs) {
                    const int nx = cx + dir[0], ny = cy + dir[1], nz = cz + dir[2];
                    if (!seen.insert(key(nx, ny, nz)).second)
                        continue;
                    const std::string n = name(nx, ny, nz);
                    if (n == "minecraft:bedrock" && !world.Peek({nx >> 4, nz >> 4}))
                        return;  // next to an unloaded chunk: decide later
                    if (IsLog(n))
                        return;
                    if (d + 1 < 6 && n.ends_with("leaves"))
                        open.push_back({nx, ny, nz, d + 1});
                }
            }
            if (RuleValue("dotiledrops"))
                DropBlockLoot(dimension, x, y, z, id, std::string(), 0.0f, nullptr, 0, 0);
            ApplyBlock(dimension, x, y, z, BlockRegistry::kAir);
            return;
        }
        case Kind::Berry: {
            const int growth = IntState(id, "growth");
            if (growth < 3 && chance(5))
                ApplyBlock(dimension, x, y, z, With(id, "growth", static_cast<int32_t>(growth + 1)));
            return;
        }
        case Kind::Wart: {
            const int age = IntState(id, "age");
            if (age < 3 && chance(10))
                ApplyBlock(dimension, x, y, z, With(id, "age", static_cast<int32_t>(age + 1)));
            return;
        }
        case Kind::Cocoa: {
            const int age = IntState(id, "age");
            if (age < 2 && chance(5))
                ApplyBlock(dimension, x, y, z, With(id, "age", static_cast<int32_t>(age + 1)));
            return;
        }
        case Kind::Kelp: {
            const int age = IntState(id, "kelp_age");
            if (age >= 25 || !chance(7) || y + 1 > range.MaxY() || !IsWater(name(x, y + 1, z)))
                return;
            ApplyBlock(dimension, x, y + 1, z, With(id, "kelp_age", static_cast<int32_t>(age + 1)));
            return;
        }
        case Kind::Copper: {
            if (!chance(1200))
                return;
            const std::string next = NextCopperStage(BlockRegistry::Instance().Name(id));
            if (next.empty())
                return;
            // Keep the block's other states (stairs facing, door halves and so on).
            ApplyBlock(dimension, x, y, z, BlockRegistry::Instance().Intern(next, BlockRegistry::Instance().States(id)));
            return;
        }
        case Kind::Budding: {
            if (!chance(5))
                return;
            static constexpr struct { int dx, dy, dz; const char* face; } kFaces[6] = {
                {0, 1, 0, "up"}, {0, -1, 0, "down"}, {0, 0, -1, "north"},
                {0, 0, 1, "south"}, {-1, 0, 0, "west"}, {1, 0, 0, "east"}};
            const auto& f = kFaces[rng() % 6];
            const int bx = x + f.dx, by = y + f.dy, bz = z + f.dz;
            if (by < range.minY || by > range.MaxY())
                return;
            const std::string there = name(bx, by, bz);
            static const char* stages[] = {"minecraft:small_amethyst_bud", "minecraft:medium_amethyst_bud",
                                           "minecraft:large_amethyst_bud", "minecraft:amethyst_cluster"};
            std::string next;
            if (IsAir(there) || IsWater(there)) {
                next = stages[0];
            } else {
                for (int i = 0; i < 3; i++)
                    if (there == stages[i])
                        next = stages[i + 1];
                if (next.empty())
                    return;
            }
            const uint16_t bud = BlockRegistry::Instance().Intern(next, {{"minecraft:block_face", std::string(f.face)}});
            ApplyBlock(dimension, bx, by, bz, bud);
            return;
        }
        default:
            return;
        }
    }

    bool Server::GrowTree(int dimension, int x, int y, int z, const std::string& kind, std::mt19937& rng) {
        ChunkManager& world = World(dimension);
        const DimensionRange range = RangeOf(dimension);
        std::string wood = kind;
        if (!BlockCatalog::Instance().Knows("minecraft:" + wood + "_log") ||
            !BlockCatalog::Instance().Knows("minecraft:" + wood + "_leaves"))
            wood = "oak";
        const bool spruce = wood == "spruce";
        const bool tall = wood == "jungle" || wood == "dark_oak" || wood == "pale_oak";
        const int height = (spruce ? 6 : tall ? 6 : wood == "birch" ? 5 : 4) + static_cast<int>(rng() % 3);
        if (y + height + 2 > range.MaxY())
            return false;
        const auto clear = [&](int bx, int by, int bz) {
            const std::string n = BlockRegistry::Instance().Name(world.GetBlock(bx, by, bz));
            return n == "minecraft:air" || n.ends_with("leaves") || n.ends_with("_sapling") || n.contains("short_grass") ||
                   n.contains("fern");
        };
        for (int dy = 1; dy <= height; dy++)
            if (!clear(x, y + dy, z))
                return false;
        const std::string ground = BlockRegistry::Instance().Name(world.GetBlock(x, y - 1, z));
        if (!(ground.contains("dirt") || ground == "minecraft:grass_block" || ground == "minecraft:podzol" ||
              ground == "minecraft:moss_block" || ground == "minecraft:mud" || ground == "minecraft:farmland" ||
              ground == "minecraft:mycelium" || ground == "minecraft:rooted_dirt"))
            return false;

        const uint16_t log = BlockRegistry::Instance().Intern("minecraft:" + wood + "_log",
                                                              {{"pillar_axis", std::string("y")}});
        const uint16_t leaves = BlockRegistry::Instance().Intern(
            "minecraft:" + wood + "_leaves", {{"persistent_bit", false}, {"update_bit", false}});
        const auto leaf = [&](int bx, int by, int bz) {
            if (by > range.MaxY())
                return;
            if (world.GetBlock(bx, by, bz) == BlockRegistry::kAir)
                ApplyBlock(dimension, bx, by, bz, leaves);
        };
        const int top = y + height;
        if (spruce) {
            int r = 0;
            for (int ly = top + 1; ly >= y + 2; ly--) {
                for (int dx = -r; dx <= r; dx++)
                    for (int dz = -r; dz <= r; dz++)
                        if (std::abs(dx) + std::abs(dz) <= r + 1 && !(std::abs(dx) == r && std::abs(dz) == r && r > 0))
                            leaf(x + dx, ly, z + dz);
                r = r >= 2 ? 1 : r + 1;
            }
        } else {
            for (int ly = top - 3; ly <= top + 1; ly++) {
                const int r = ly <= top - 2 ? 2 : 1;
                for (int dx = -r; dx <= r; dx++)
                    for (int dz = -r; dz <= r; dz++) {
                        const bool corner = std::abs(dx) == r && std::abs(dz) == r;
                        if (corner && (ly == top + 1 || rng() % 2 == 0))
                            continue;
                        leaf(x + dx, ly, z + dz);
                    }
            }
        }
        for (int dy = 0; dy < height; dy++)
            ApplyBlock(dimension, x, y + dy, z, log);
        if (ground == "minecraft:grass_block" || ground == "minecraft:farmland" || ground == "minecraft:mycelium")
            ApplyBlock(dimension, x, y - 1, z, BlockCatalog::Instance().DefaultBlock("minecraft:dirt"));
        return true;
    }
}
