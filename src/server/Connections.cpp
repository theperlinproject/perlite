// Iron bars, glass panes, fences and walls carry their connections as block states, which the
// server works out from the neighbours (the client draws what the states say). The rules are the
// game's: panes and bars join each other, walls and sturdy blocks; fences join fences of their kind,
// fence gates across their path and sturdy blocks; walls join walls, panes, gates and sturdy blocks,
// rising tall where something above covers the side and keeping their post where the line is not
// straight or something above needs one.
#include "server/Server.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace perlite {
    namespace {
        enum Family : int8_t {
            Unknown = 0,
            None,      // nothing that joins, and not sturdy
            Sturdy,    // a full block: everything joins it
            Exception, // full, but nothing joins it (leaves, pumpkins, melons, shulker boxes, barriers)
            Pane,      // glass panes and iron bars
            WoodFence,
            NetherFence,
            Wall,
            Gate,
            PostOverride, // above a wall, keeps its post: torches, lanterns, standing signs and banners
        };

        bool Ends(std::string_view s, std::string_view t) { return s.size() >= t.size() && s.substr(s.size() - t.size()) == t; }
        bool Has(std::string_view s, std::string_view t) { return s.find(t) != std::string_view::npos; }

        Family Classify(uint16_t id) {
            if (id == BlockRegistry::kAir)
                return None;
            const std::string full = BlockRegistry::Instance().Name(id);
            std::string_view n = full;
            if (n.starts_with("minecraft:"))
                n.remove_prefix(10);
            if (n == "iron_bars" || Ends(n, "glass_pane"))
                return Pane;
            if (n == "nether_brick_fence")
                return NetherFence;
            if (Ends(n, "_fence"))
                return WoodFence;
            if (n == "fence_gate" || Ends(n, "_fence_gate"))
                return Gate;
            if (Ends(n, "_wall") && !Has(n, "_sign") && !Has(n, "_banner") && !Has(n, "torch") && !Has(n, "_head") &&
                !Has(n, "_skull") && !Has(n, "_fan"))
                return Wall;
            if (Has(n, "torch") || n == "lantern" || n == "soul_lantern" || n == "standing_sign" || Ends(n, "_standing_sign") ||
                n == "standing_banner" || Has(n, "pressure_plate"))
                return PostOverride;
            if (Has(n, "leaves") || n == "pumpkin" || n == "carved_pumpkin" || n == "lit_pumpkin" || n == "melon_block" ||
                Has(n, "shulker_box") || n == "barrier")
                return Exception;
            if (!BlockSolid(id))
                return None;
            static constexpr std::string_view partial[] = {
                "slab", "stairs", "door", "trapdoor", "chest", "bed", "cake", "carpet", "ladder", "lantern", "chain", "candle",
                "flower_pot", "skull", "head", "button", "anvil", "cactus", "hopper", "cauldron", "bell", "campfire", "farmland",
                "grass_path", "dirt_path", "snow_layer", "enchanting_table", "brewing_stand", "end_portal_frame", "daylight_detector",
                "repeater", "comparator", "lectern", "stonecutter", "grindstone", "scaffolding", "composter", "conduit",
                "dragon_egg", "piston", "sea_pickle", "turtle_egg", "sniffer_egg", "frog_spawn", "amethyst_bud", "amethyst_cluster",
                "pointed_dripstone", "lightning_rod", "decorated_pot", "heavy_core", "end_rod", "chorus", "bamboo", "azalea",
                "honey_block", "soul_sand", "mud", "sign", "banner", "rail", "bars", "pane", "fence", "wall", "hanging", "pot",
                "vault", "trial_spawner", "copper_golem_statue", "shelf"};
            for (std::string_view p : partial)
                if (Has(n, p) && !(p == "mud" && (Has(n, "brick") || n == "packed_mud")) &&
                    !(p == "bamboo" && (Has(n, "block") || Has(n, "planks") || Has(n, "mosaic"))))
                    return None;
            return Sturdy;
        }

        Family FamilyOf(uint16_t id) {
            static std::atomic<int8_t> cache[0x10000] = {};
            if (const int8_t f = cache[id].load(std::memory_order_relaxed))
                return static_cast<Family>(f);
            const Family f = Classify(id);
            cache[id].store(static_cast<int8_t>(f), std::memory_order_relaxed);
            return f;
        }

        bool Connectable(uint16_t id) {
            const Family f = FamilyOf(id);
            return f == Pane || f == WoodFence || f == NetherFence || f == Wall;
        }

        // north, east, south, west (the order of the state names below)
        constexpr int kDx[4] = {0, 1, 0, -1};
        constexpr int kDz[4] = {-1, 0, 1, 0};
        constexpr const char* kConnection[4] = {"minecraft:connection_north", "minecraft:connection_east",
                                                "minecraft:connection_south", "minecraft:connection_west"};
        constexpr const char* kWallSide[4] = {"wall_connection_type_north", "wall_connection_type_east",
                                              "wall_connection_type_south", "wall_connection_type_west"};

        int IntState(uint16_t id, std::string_view key) {
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == key) {
                    if (const bool* b = std::get_if<bool>(&v))
                        return *b ? 1 : 0;
                    if (const int32_t* i = std::get_if<int32_t>(&v))
                        return *i;
                }
            return -1;
        }

        std::string TextState(uint16_t id, std::string_view key) {
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == key)
                    if (const std::string* s = std::get_if<std::string>(&v))
                        return *s;
            return {};
        }

        // A fence gate lets a neighbour join it when the gate lies across the line between them.
        bool GateAcross(uint16_t gate, int side) {
            const int dir = IntState(gate, "direction"); // 0 south, 1 west, 2 north, 3 east
            const bool alongX = dir == 0 || dir == 2;      // a gate facing north/south spans east-west
            const bool sideAlongX = side == 1 || side == 3;
            return alongX == sideAlongX;
        }

        void SetBit(StateList& states, std::string_view key, bool on) {
            for (auto& [k, v] : states)
                if (k == key) {
                    if (std::holds_alternative<bool>(v))
                        v = on;
                    else
                        v = static_cast<int32_t>(on ? 1 : 0);
                }
        }

        void SetText(StateList& states, std::string_view key, std::string value) {
            for (auto& [k, v] : states)
                if (k == key)
                    v = std::move(value);
        }

        // Whether the block above a wall covers the given side (making it tall): a full block, or a
        // wall, pane or fence above that reaches out the same way.
        bool AboveCovers(uint16_t above, int side) {
            const Family f = FamilyOf(above);
            if (f == Sturdy || f == Exception)
                return true;
            if (f == Wall)
                return TextState(above, kWallSide[side]) != "none" && !TextState(above, kWallSide[side]).empty();
            if (f == Pane || f == WoodFence || f == NetherFence)
                return IntState(above, kConnection[side]) == 1;
            if (f == Gate)
                return GateAcross(above, side);
            return false;
        }
    }

    uint16_t Server::ConnectedState(int dimension, int x, int y, int z, uint16_t self) {
        const Family mine = FamilyOf(self);
        if (mine != Pane && mine != WoodFence && mine != NetherFence && mine != Wall)
            return self;
        ChunkManager& world = World(dimension);
        bool joins[4];
        for (int side = 0; side < 4; side++) {
            const int nx = x + kDx[side], nz = z + kDz[side];
            const uint16_t n = world.Peek({nx >> 4, nz >> 4}) ? world.GetBlock(nx, y, nz) : BlockRegistry::kAir;
            const Family f = FamilyOf(n);
            bool j = f == Sturdy;
            switch (mine) {
            case Pane:
                j = j || f == Pane || f == Wall;
                break;
            case WoodFence:
            case NetherFence:
                j = j || f == mine || (f == Gate && GateAcross(n, side));
                break;
            case Wall:
                j = j || f == Wall || f == Pane || (f == Gate && GateAcross(n, side));
                break;
            default:
                break;
            }
            joins[side] = j;
        }
        // Every state the block has, from its default, so a block written without its connection
        // states (a structure's, a plugin's) gets them.
        const std::string name = BlockRegistry::Instance().Name(self);
        StateList states = BlockRegistry::Instance().States(BlockCatalog::Instance().DefaultBlock(name));
        for (const auto& [k, v] : BlockRegistry::Instance().States(self))
            for (auto& [dk, dv] : states)
                if (dk == k)
                    dv = v;
        if (states.empty())
            states = BlockRegistry::Instance().States(self);
        if (mine != Wall) {
            for (int side = 0; side < 4; side++)
                SetBit(states, kConnection[side], joins[side]);
        } else {
            const uint16_t above = world.GetBlock(x, y + 1, z);
            std::string sides[4];
            for (int side = 0; side < 4; side++)
                sides[side] = !joins[side] ? "none" : AboveCovers(above, side) ? "tall" : "short";
            for (int side = 0; side < 4; side++)
                SetText(states, kWallSide[side], sides[side]);
            const bool n = sides[0] != "none", e = sides[1] != "none", s = sides[2] != "none", w = sides[3] != "none";
            bool post;
            if (FamilyOf(above) == Wall && IntState(above, "wall_post_bit") == 1)
                post = true;
            else if ((!n && !e && !s && !w) || n != s || e != w)
                post = true;
            else if ((sides[0] == "tall" && sides[2] == "tall") || (sides[1] == "tall" && sides[3] == "tall"))
                post = false;
            else {
                const Family fa = FamilyOf(above);
                post = fa == PostOverride || fa == Sturdy || fa == Exception;
            }
            SetBit(states, "wall_post_bit", post);
        }
        return BlockRegistry::Instance().Intern(name, std::move(states));
    }

    void Server::RefreshConnection(int dimension, int x, int y, int z) {
        ChunkManager& world = World(dimension);
        if (!world.Peek({x >> 4, z >> 4}))
            return;
        const uint16_t current = world.GetBlock(x, y, z);
        if (!Connectable(current))
            return;
        const uint16_t next = ConnectedState(dimension, x, y, z, current);
        if (next == current)
            return;
        world.SetBlock(x, y, z, next);
        QueueBlockUpdate(dimension, x, y, z, next, 0);
    }

    // A block changed: it and the blocks that may join it look again (the wall below too, whose
    // height and post follow what stands on it).
    void Server::RefreshConnectionsAround(int dimension, int x, int y, int z) {
        RefreshConnection(dimension, x, y, z);
        for (int side = 0; side < 4; side++)
            RefreshConnection(dimension, x + kDx[side], y, z + kDz[side]);
        RefreshConnection(dimension, x, y - 1, z);
    }

    // A chunk fresh from the generator: its bars, panes, fences and walls take their connections,
    // and those along the edges of the loaded chunks beside it look again. Top down, so a wall sees
    // the finished state of the wall above it.
    void Server::ConnectChunk(int dimension, ChunkPos pos) {
        ChunkManager& world = World(dimension);
        const auto entry = world.Peek(pos);
        if (!entry)
            return;
        const DimensionRange range = RangeOf(dimension);
        const ChunkColumn& col = entry->column;
        for (int sec = range.FirstSection() + range.SectionCount() - 1; sec >= range.FirstSection(); sec--) {
            const ChunkColumn::Section& s = col.SectionAt(sec);
            if (!s.cells) {
                if (!Connectable(s.uniform))
                    continue;
            } else {
                bool any = false;
                uint16_t last = 0xFFFF;
                for (const uint16_t b : *s.cells)
                    if (b != last) {
                        last = b;
                        if (Connectable(b)) {
                            any = true;
                            break;
                        }
                    }
                if (!any)
                    continue;
            }
            for (int ly = 15; ly >= 0; ly--)
                for (int lx = 0; lx < 16; lx++)
                    for (int lz = 0; lz < 16; lz++) {
                        const int y = kMinY + sec * 16 + ly;
                        const uint16_t b = col.Get(lx, y, lz);
                        if (Connectable(b))
                            RefreshConnection(dimension, pos.x * 16 + lx, y, pos.z * 16 + lz);
                    }
        }
        // The neighbouring chunks' edge columns facing this one.
        for (int side = 0; side < 4; side++) {
            const ChunkPos other{pos.x + kDx[side], pos.z + kDz[side]};
            const auto o = world.Peek(other);
            if (!o)
                continue;
            for (int i = 0; i < 16; i++) {
                int x, z;
                if (kDx[side] != 0) {
                    x = kDx[side] > 0 ? other.x * 16 : other.x * 16 + 15;
                    z = pos.z * 16 + i;
                } else {
                    z = kDz[side] > 0 ? other.z * 16 : other.z * 16 + 15;
                    x = pos.x * 16 + i;
                }
                for (int y = range.MaxY(); y >= range.minY; y--)
                    if (Connectable(world.GetBlock(x, y, z)))
                        RefreshConnection(dimension, x, y, z);
            }
        }
    }
}
