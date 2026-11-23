// Structures the game builds in code rather than from templates, and the small structure features.
// Their layouts follow the game's pieces (sizes, materials, loot tables, where the chests and spawners
// are), drawn here block by block; the exact piece-by-piece randomness of the game's generators is
// not reproduced, so a seed gives the same kind of structure in the same place, laid out differently.
#include "world/level/structure/CodeStructures.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <numbers>

namespace perlite::structure {
    Builder::Builder(std::string name, int sx, int sy, int sz) : t(std::make_shared<Template>()) {
        t->name = std::move(name);
        t->sx = sx;
        t->sy = sy;
        t->sz = sz;
    }

    bool Builder::InBounds(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < t->sx && y < t->sy && z < t->sz; }

    uint16_t Builder::Index(std::string_view state) {
        const std::string key(state);
        if (const auto it = palette.find(key); it != palette.end())
            return it->second;
        const uint16_t i = static_cast<uint16_t>(t->palette.size());
        JavaState s = ParseState(key);
        const std::string bare = StripNamespace(s.name);
        t->isAir.push_back(bare == "air");
        t->isJigsaw.push_back(false);
        t->isStructureBlock.push_back(false);
        t->palette.push_back(std::move(s));
        palette[key] = i;
        return i;
    }

    void Builder::Set(int x, int y, int z, std::string_view state) {
        if (!InBounds(x, y, z) || state.empty())
            return;
        const uint16_t s = Index(state);
        const auto key = std::make_tuple(x, y, z);
        if (const auto it = at.find(key); it != at.end()) {
            t->blocks[it->second].state = s;
            t->blocks[it->second].nbt = -1;
            return;
        }
        at[key] = t->blocks.size();
        t->blocks.push_back({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z), s, -1});
    }

    void Builder::Fill(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view state) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++)
            for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++)
                for (int z = std::min(z0, z1); z <= std::max(z0, z1); z++)
                    Set(x, y, z, state);
    }

    void Builder::Hollow(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view wall, std::string_view inside) {
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++) {
                    const bool edge = x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1;
                    if (edge)
                        Set(x, y, z, wall);
                    else if (!inside.empty())
                        Set(x, y, z, inside);
                }
    }

    void Builder::Chest(int x, int y, int z, std::string_view facing, std::string_view loot) {
        Set(x, y, z, std::string("minecraft:chest[facing=") + std::string(facing) + "]");
        const auto it = at.find(std::make_tuple(x, y, z));
        if (it == at.end())
            return;
        nbt::Value tag = nbt::Value::Make(nbt::Compound);
        tag.SetString("LootTable", std::string("minecraft:") + std::string(loot));
        t->blocks[it->second].nbt = static_cast<int32_t>(t->nbts.size());
        t->nbts.push_back(std::move(tag));
    }

    void Builder::Spawner(int x, int y, int z, std::string_view mob) {
        Set(x, y, z, "minecraft:spawner");
        (void)mob; // the spawner's mob is not kept by the block yet
    }

    void Builder::Mob(int x, int y, int z, std::string_view id) {
        if (InBounds(x, y, z))
            t->markers.push_back({x, y, z, std::string(id)});
    }

    std::shared_ptr<const Template> Builder::Done() {
        std::ranges::stable_sort(t->blocks, [](const TemplateBlock& a, const TemplateBlock& b) {
            if (a.y != b.y)
                return a.y < b.y;
            if (a.x != b.x)
                return a.x < b.x;
            return a.z < b.z;
        });
        at.clear();
        return t;
    }

    namespace {
        constexpr double kPi = std::numbers::pi;

        void Turned(int turns, int x, int z, int& ox, int& oz) {
            switch (turns & 3) {
            case 1:
                ox = -z, oz = x;
                break;
            case 2:
                ox = -x, oz = -z;
                break;
            case 3:
                ox = z, oz = -x;
                break;
            default:
                ox = x, oz = z;
            }
        }

        void AddPiece(Start& start, std::shared_ptr<const Template> t, int turns, int x, int y, int z) {
            Piece p;
            p.tmpl = std::move(t);
            p.turns = turns;
            p.x = x;
            p.y = y;
            p.z = z;
            int ax, az, bx, bz;
            Turned(turns, 0, 0, ax, az);
            Turned(turns, p.tmpl->sx - 1, p.tmpl->sz - 1, bx, bz);
            p.box = {std::min(ax, bx) + x, y, std::min(az, bz) + z, std::max(ax, bx) + x, y + p.tmpl->sy - 1, std::max(az, bz) + z};
            start.pieces.push_back(std::move(p));
        }

        bool Clashes(const Start& start, const Box& b) {
            for (const Piece& p : start.pieces)
                if (p.box.Intersects(b))
                    return true;
            return false;
        }

        bool BiomeIn(const WorldProbe& probe, int x, int y, int z, std::initializer_list<std::string_view> names) {
            const int b = probe.biome(x, y, z);
            if (b < 0 || b >= vanilla::kBiomeCount)
                return false;
            for (std::string_view n : names)
                if (n == vanilla::kBiomes[b].java)
                    return true;
            return false;
        }

        const char* Pick(WorldRandom& r, std::initializer_list<const char*> options) {
            const int n = static_cast<int>(options.size());
            return options.begin()[r.NextInt(n)];
        }

        std::string StoneBrick(WorldRandom& r) {
            const float f = r.NextFloat();
            return f < 0.2f ? "minecraft:cracked_stone_bricks" : f < 0.5f ? "minecraft:mossy_stone_bricks" : "minecraft:stone_bricks";
        }
    }

    // --- Desert pyramid -------------------------------------------------------------------------
    bool BuildDesertPyramid(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x = chunkX * 16, z = chunkZ * 16;
        const int ground = probe.surface(x + 10, z + 10);
        if (!BiomeIn(probe, x + 10, ground, z + 10, {"desert"}))
            return false;
        Builder b("code/desert_pyramid", 21, 27, 21);
        const int g = 12; // ground floor height in the template
        // The hidden chamber, twelve blocks down: four chests and the TNT under the pressure plate.
        b.Fill(7, 0, 7, 13, g - 1, 13, "minecraft:sandstone");
        b.Fill(8, 2, 8, 12, 5, 12, "minecraft:air");
        b.Fill(9, 1, 9, 11, 1, 11, "minecraft:tnt");
        b.Set(10, 2, 10, "minecraft:stone_pressure_plate");
        b.Chest(10, 2, 8, "south", "chests/desert_pyramid");
        b.Chest(10, 2, 12, "north", "chests/desert_pyramid");
        b.Chest(8, 2, 10, "east", "chests/desert_pyramid");
        b.Chest(12, 2, 10, "west", "chests/desert_pyramid");
        for (int y = 2; y <= 5; y++)
            for (const int c : {8, 12})
                b.Set(c, y, c, "minecraft:cut_sandstone");
        b.Fill(10, 6, 10, 10, g - 1, 10, "minecraft:air"); // the shaft under the floor's centre
        // The pyramid: stepped layers, hollow inside.
        for (int k = 0; k <= 9; k++) {
            const int y = g + k;
            b.Fill(k, y, k, 20 - k, y, 20 - k, "minecraft:sandstone");
            if (k < 8 && 20 - k - (k + 1) > 1)
                b.Fill(k + 1, y, k + 1, 19 - k, y, 19 - k, k == 0 ? "minecraft:sandstone" : "minecraft:air");
        }
        b.Fill(1, g, 1, 19, g, 19, "minecraft:sandstone");
        // The floor's pattern: orange terracotta around blue at the centre (over the shaft).
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++)
                if (std::abs(dx) == 2 || std::abs(dz) == 2 || (dx == 0) != (dz == 0))
                    b.Set(10 + dx, g, 10 + dz, "minecraft:orange_terracotta");
        b.Set(10, g, 10, "minecraft:blue_terracotta");
        // The two front towers and the entrance between them.
        for (const int tx : {0, 16}) {
            b.Hollow(tx, g, 0, tx + 4, g + 9, 4, "minecraft:sandstone", "minecraft:air");
            for (int y = g + 2; y <= g + 8; y += 3)
                b.Fill(tx, y, 0, tx + 4, y, 0, "minecraft:orange_terracotta");
            b.Fill(tx + 1, g + 10, 1, tx + 3, g + 10, 3, "minecraft:cut_sandstone");
            b.Fill(tx + 2, g + 1, 4, tx + 2, g + 3, 4, "minecraft:air");
        }
        b.Fill(8, g + 1, 0, 12, g + 4, 0, "minecraft:sandstone");
        b.Fill(9, g + 1, 0, 11, g + 3, 1, "minecraft:air");
        b.Fill(9, g + 5, 0, 11, g + 5, 0, "minecraft:chiseled_sandstone");
        const int turns = r.NextInt(4);
        int ox, oz;
        Turned(turns, 10, 10, ox, oz);
        AddPiece(start, b.Done(), turns, x + 10 - ox, ground - g - 1, z + 10 - oz);
        return true;
    }

    // --- Jungle pyramid -------------------------------------------------------------------------
    bool BuildJunglePyramid(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x = chunkX * 16, z = chunkZ * 16;
        const int ground = probe.surface(x + 6, z + 7);
        if (!BiomeIn(probe, x + 6, ground, z + 7, {"jungle", "bamboo_jungle"}))
            return false;
        Builder b("code/jungle_pyramid", 12, 14, 15);
        const auto stone = [&] { return r.NextFloat() < 0.4f ? "minecraft:mossy_cobblestone" : "minecraft:cobblestone"; };
        const auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1, bool hollow) {
            for (int xx = x0; xx <= x1; xx++)
                for (int yy = y0; yy <= y1; yy++)
                    for (int zz = z0; zz <= z1; zz++) {
                        const bool edge = xx == x0 || xx == x1 || yy == y0 || yy == y1 || zz == z0 || zz == z1;
                        b.Set(xx, yy, zz, !hollow || edge ? stone() : "minecraft:air");
                    }
        };
        box(0, 0, 0, 11, 4, 14, true);    // the lower floor, half underground
        box(1, 4, 1, 10, 8, 13, true);    // the main hall
        box(2, 8, 2, 9, 11, 12, true);    // the upper storey
        box(3, 11, 4, 8, 13, 10, false);  // the roof
        b.Fill(5, 5, 1, 6, 7, 1, "minecraft:air"); // entrance
        b.Fill(5, 4, 0, 6, 4, 0, "minecraft:cobblestone_stairs[facing=south]");
        for (int y = 1; y <= 3; y++) // stairs down to the lower floor
            b.Fill(5 + 0, y, 10 - y, 6, y, 10 - y, "minecraft:cobblestone_stairs[facing=north]");
        b.Fill(5, 4, 7, 6, 4, 9, "minecraft:air");
        // The arrow trap: tripwire across the lower corridor, a dispenser in the wall.
        b.Set(1, 1, 4, "minecraft:tripwire_hook[facing=east]");
        b.Fill(2, 1, 4, 9, 1, 4, "minecraft:tripwire");
        b.Set(10, 1, 4, "minecraft:tripwire_hook[facing=west]");
        b.Set(10, 2, 6, "minecraft:dispenser[facing=west]");
        b.Chest(2, 1, 12, "east", "chests/jungle_temple");
        // The lever puzzle behind which the second chest waits.
        for (const int lx : {7, 8, 9})
            b.Set(lx, 2, 13, "minecraft:lever[face=wall,facing=north]");
        b.Chest(9, 1, 11, "west", "chests/jungle_temple");
        b.Set(7, 1, 11, "minecraft:sticky_piston[facing=up]");
        const int turns = r.NextInt(4);
        AddPiece(start, b.Done(), turns, x, ground - 4, z);
        return true;
    }

    // --- Swamp hut ------------------------------------------------------------------------------
    bool BuildSwampHut(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x = chunkX * 16, z = chunkZ * 16;
        const int ground = probe.surface(x + 3, z + 4);
        if (!BiomeIn(probe, x + 3, ground, z + 4, {"swamp"}))
            return false;
        Builder b("code/swamp_hut", 7, 12, 9);
        const int f = 5; // the floor
        for (const int lx : {1, 5})
            for (const int lz : {2, 7})
                b.Fill(lx, 0, lz, lx, f + 3, lz, "minecraft:oak_log");
        b.Fill(1, f, 1, 5, f, 7, "minecraft:spruce_planks");
        b.Fill(1, f + 1, 2, 5, f + 3, 7, "minecraft:spruce_planks");
        b.Fill(2, f + 1, 3, 4, f + 3, 6, "minecraft:air");
        b.Fill(3, f + 1, 2, 3, f + 2, 2, "minecraft:air"); // the door gap
        b.Set(1, f + 2, 4, "minecraft:air");
        b.Set(5, f + 2, 4, "minecraft:air");
        b.Fill(1, f + 1, 1, 5, f + 1, 1, "minecraft:oak_fence");
        b.Fill(0, f + 4, 1, 6, f + 4, 8, "minecraft:spruce_planks");
        b.Fill(0, f + 4, 0, 6, f + 4, 0, "minecraft:spruce_stairs[facing=south]");
        b.Fill(0, f + 4, 8, 6, f + 4, 8, "minecraft:spruce_stairs[facing=north]");
        b.Set(4, f + 1, 6, "minecraft:cauldron");
        b.Set(2, f + 1, 6, "minecraft:crafting_table");
        b.Set(2, f + 2, 3, "minecraft:potted_red_mushroom");
        b.Mob(3, f + 1, 4, "minecraft:witch");
        b.Mob(3, f + 1, 5, "minecraft:cat");
        (void)r;
        AddPiece(start, b.Done(), r.NextInt(4), x, ground - f, z);
        return true;
    }

    // --- Buried treasure ------------------------------------------------------------------------
    bool BuildBuriedTreasure(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x = chunkX * 16 + 9, z = chunkZ * 16 + 9;
        const int ground = probe.ground ? probe.ground(x, z) : probe.surface(x, z);
        if (!BiomeIn(probe, x, ground, z, {"beach", "snowy_beach"}))
            return false;
        Builder b("code/buried_treasure", 3, 3, 3);
        b.Fill(0, 0, 0, 2, 2, 2, "minecraft:sand");
        b.Chest(1, 1, 1, "north", "chests/buried_treasure");
        (void)r;
        AddPiece(start, b.Done(), 0, x - 1, ground - 4, z - 1);
        return true;
    }

    // --- Mineshaft ------------------------------------------------------------------------------
    bool BuildMineshaft(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x0 = chunkX * 16 + 2, z0 = chunkZ * 16 + 2;
        const int y0 = 10 + r.NextInt(30);
        const bool mesa = BiomeIn(probe, x0, y0 + 40, z0, {"badlands", "eroded_badlands", "wooded_badlands"});
        const char* planks = mesa ? "minecraft:dark_oak_planks" : "minecraft:oak_planks";
        const char* fence = mesa ? "minecraft:dark_oak_fence" : "minecraft:oak_fence";
        // The room the shaft starts from: a dirt-floored cave.
        {
            const int w = 7 + r.NextInt(6), d = 7 + r.NextInt(6), h = 4 + r.NextInt(3);
            Builder b("code/mineshaft_room", w, h, d);
            b.Fill(0, 0, 0, w - 1, 0, d - 1, "minecraft:dirt");
            b.Fill(0, 1, 0, w - 1, h - 1, d - 1, "minecraft:air");
            AddPiece(start, b.Done(), 0, x0, y0, z0);
        }
        struct Tunnel {
            int x, y, z, dx, dz, depth;
        };
        const Box room = start.pieces.back().box;
        std::vector<Tunnel> todo = {{room.x1 + 1, y0, room.z0 + 2, 1, 0, 0}, {room.x0 - 1, y0, room.z0 + 2, -1, 0, 0},
                                    {room.x0 + 2, y0, room.z1 + 1, 0, 1, 0}, {room.x0 + 2, y0, room.z0 - 1, 0, -1, 0}};
        int made = 0;
        while (!todo.empty() && made < 40) {
            const Tunnel t = todo.back();
            todo.pop_back();
            if (t.depth > 8 || std::abs(t.x - x0) > 80 || std::abs(t.z - z0) > 80)
                continue;
            const int length = 5 * (2 + r.NextInt(4));
            const bool alongX = t.dx != 0;
            const int sx = alongX ? length : 3, sz = alongX ? 3 : length;
            const int bx = t.dx < 0 ? t.x - length + 1 : t.dx > 0 ? t.x : t.x - 1;
            const int bz = t.dz < 0 ? t.z - length + 1 : t.dz > 0 ? t.z : t.z - 1;
            const Box box{bx, t.y, bz, bx + sx - 1, t.y + 2, bz + sz - 1};
            if (Clashes(start, box))
                continue;
            Builder b("code/mineshaft_corridor", sx, 3, sz);
            b.Fill(0, 0, 0, sx - 1, 2, sz - 1, "minecraft:air");
            for (int i = 0; i < length; i++) {
                const int lx = alongX ? i : 1, lz = alongX ? 1 : i;
                if (r.NextFloat() < 0.7f)
                    b.Set(lx, 0, lz, std::string("minecraft:rail[shape=") + (alongX ? "east_west" : "north_south") + "]");
                if (i % 5 == 2) {
                    // A support: two posts and a beam.
                    const int ax = alongX ? i : 0, az = alongX ? 0 : i, cx = alongX ? i : 2, cz = alongX ? 2 : i;
                    b.Fill(ax, 0, az, ax, 1, az, fence);
                    b.Fill(cx, 0, cz, cx, 1, cz, fence);
                    b.Fill(ax, 2, az, cx, 2, cz, planks);
                    if (r.NextInt(4) == 0)
                        b.Set(alongX ? i : 1, 1, alongX ? 1 : i, "minecraft:torch");
                }
                for (int w = 0; w < 3; w++)
                    if (r.NextFloat() < 0.05f)
                        b.Set(alongX ? i : w, 2, alongX ? w : i, "minecraft:cobweb");
            }
            if (r.NextInt(23) == 0) {
                // A chest (the game puts it in a minecart on the rails).
                const int i = 1 + r.NextInt(length - 2);
                b.Chest(alongX ? i : 1, 0, alongX ? 1 : i, alongX ? "north" : "east", "chests/abandoned_mineshaft");
            }
            if (!mesa && r.NextInt(23) == 0) {
                // A cave spider nest.
                const int i = 1 + r.NextInt(length - 2);
                b.Spawner(alongX ? i : 1, 1, alongX ? 1 : i, "minecraft:cave_spider");
                for (int k = 0; k < 12; k++)
                    b.Set(alongX ? std::clamp(i - 2 + r.NextInt(5), 0, length - 1) : r.NextInt(3), r.NextInt(3),
                          alongX ? r.NextInt(3) : std::clamp(i - 2 + r.NextInt(5), 0, length - 1), "minecraft:cobweb");
            }
            AddPiece(start, b.Done(), 0, bx, t.y, bz);
            made++;
            // Onward: straight on, or turning left and right at the end, sometimes a level up or down.
            const int ex = t.x + t.dx * length, ez = t.z + t.dz * length;
            const int ny = std::clamp(t.y + (r.NextInt(5) == 0 ? (r.NextInt(2) ? 4 : -4) : 0), 5, 50);
            if (r.NextInt(3) != 0)
                todo.push_back({ex, ny, ez, t.dx, t.dz, t.depth + 1});
            if (r.NextInt(2) == 0)
                todo.push_back({ex - t.dx * 2 + t.dz * 2, t.y, ez - t.dz * 2 + t.dx * 2, t.dz, t.dx, t.depth + 1});
            if (r.NextInt(2) == 0)
                todo.push_back({ex - t.dx * 2 - t.dz * 2, t.y, ez - t.dz * 2 - t.dx * 2, -t.dz, -t.dx, t.depth + 1});
        }
        return true;
    }

    // --- Stronghold ---------------------------------------------------------------------------
    const std::vector<std::pair<int, int>>& StrongholdChunks(int64_t seed) {
        static std::mutex mutex;
        static std::map<int64_t, std::vector<std::pair<int, int>>> cache;
        std::lock_guard lock(mutex);
        auto& out = cache[seed];
        if (!out.empty())
            return out;
        // The game's concentric rings: distance 32, spread 3, count 128.
        WorldRandom random = WorldRandom::Legacy(seed);
        int distance = 32, spread = 3;
        const int count = 128;
        double angle = random.NextDouble() * kPi * 2.0;
        int placed = 0, ring = 0;
        for (int i = 0; i < count; i++) {
            const double d = 4.0 * distance + distance * ring * 6.0 + (random.NextDouble() - 0.5) * (distance * 2.5);
            out.push_back({static_cast<int>(std::lround(std::cos(angle) * d)), static_cast<int>(std::lround(std::sin(angle) * d))});
            angle += 2.0 * kPi / spread;
            if (++placed == spread) {
                ring++;
                placed = 0;
                spread += 2 * spread / (ring + 1);
                spread = std::min(spread, count - i);
                angle += random.NextDouble() * kPi * 2.0;
            }
        }
        return out;
    }

    namespace {
        // A stronghold room of stone bricks with a doorway on each side it connects to.
        std::shared_ptr<const Template> StrongholdBox(WorldRandom& r, const char* name, int sx, int sy, int sz,
                                                      const std::function<void(Builder&)>& inside) {
            Builder b(name, sx, sy, sz);
            for (int x = 0; x < sx; x++)
                for (int y = 0; y < sy; y++)
                    for (int z = 0; z < sz; z++) {
                        const bool edge = x == 0 || x == sx - 1 || y == 0 || y == sy - 1 || z == 0 || z == sz - 1;
                        b.Set(x, y, z, edge ? StoneBrick(r) : "minecraft:air");
                    }
            inside(b);
            return b.Done();
        }
    }

    bool BuildStronghold(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int cx = chunkX * 16 + 8, cz = chunkZ * 16 + 8;
        const int y = 10 + r.NextInt(25);
        // Pieces along corridors from a central crossing; one branch ends in the portal room.
        AddPiece(start, StrongholdBox(r, "code/stronghold_crossing", 11, 7, 11, [&](Builder& b) {
                     for (const auto& [dx, dz] : std::initializer_list<std::pair<int, int>>{{5, 0}, {5, 10}, {0, 5}, {10, 5}})
                         b.Fill(dx == 0 || dx == 10 ? dx : 4, 1, dz == 0 || dz == 10 ? dz : 4, dx == 0 || dx == 10 ? dx : 6, 3,
                                dz == 0 || dz == 10 ? dz : 6, "minecraft:air");
                     b.Set(5, 5, 5, "minecraft:torch");
                     b.Fill(4, 1, 4, 6, 1, 6, "minecraft:stone_brick_slab");
                 }),
                 0, cx - 5, y, cz - 5);
        // The spiral stairs up from the crossing towards the surface.
        {
            Builder b("code/stronghold_stairs", 5, 11, 5);
            for (int yy = 0; yy < 11; yy++)
                for (int x = 0; x < 5; x++)
                    for (int z = 0; z < 5; z++)
                        b.Set(x, yy, z, x == 0 || x == 4 || z == 0 || z == 4 ? StoneBrick(r) : "minecraft:air");
            static const int kSpiral[8][2] = {{1, 1}, {2, 1}, {3, 1}, {3, 2}, {3, 3}, {2, 3}, {1, 3}, {1, 2}};
            for (int yy = 0; yy < 10; yy++) {
                const auto& s = kSpiral[yy % 8];
                b.Set(s[0], yy, s[1], "minecraft:stone_brick_slab");
                b.Set(2, yy, 2, "minecraft:stone_bricks");
            }
            AddPiece(start, b.Done(), 0, cx - 2, y + 6, cz - 2);
        }
        struct Exit {
            int x, z, dx, dz, depth;
        };
        std::vector<Exit> exits = {{cx + 6, cz, 1, 0, 0}, {cx - 6, cz, -1, 0, 0}, {cx, cz + 6, 0, 1, 0}, {cx, cz - 6, 0, -1, 0}};
        const int portalBranch = r.NextInt(4);
        bool portal = false;
        int pieces = 0;
        for (size_t branch = 0; branch < exits.size() && pieces < 30; branch++) {
            Exit e = exits[branch];
            const bool toPortal = static_cast<int>(branch) == portalBranch;
            for (int step = 0; step < 4 && pieces < 30; step++) {
                // A corridor.
                const int length = 7 + r.NextInt(9);
                const bool alongX = e.dx != 0;
                const int sx = alongX ? length : 5, sz = alongX ? 5 : length;
                const int bx = e.dx < 0 ? e.x - length + 1 : e.dx > 0 ? e.x : e.x - 2;
                const int bz = e.dz < 0 ? e.z - length + 1 : e.dz > 0 ? e.z : e.z - 2;
                const Box box{bx, y, bz, bx + sx - 1, y + 4, bz + sz - 1};
                if (Clashes(start, box))
                    break;
                const bool chest = r.NextInt(5) == 0;
                AddPiece(start, StrongholdBox(r, "code/stronghold_corridor", sx, 5, sz, [&](Builder& b) {
                             if (alongX) {
                                 b.Fill(0, 1, 1, 0, 3, 3, "minecraft:air");
                                 b.Fill(sx - 1, 1, 1, sx - 1, 3, 3, "minecraft:air");
                             } else {
                                 b.Fill(1, 1, 0, 3, 3, 0, "minecraft:air");
                                 b.Fill(1, 1, sz - 1, 3, 3, sz - 1, "minecraft:air");
                             }
                             if (chest)
                                 b.Chest(alongX ? sx / 2 : 1, 1, alongX ? 1 : sz / 2, alongX ? "south" : "east", "chests/stronghold_corridor");
                             b.Set(alongX ? sx / 2 : 2, 3, alongX ? 2 : sz / 2, "minecraft:torch");
                         }),
                         0, bx, y, bz);
                pieces++;
                e.x += e.dx * length;
                e.z += e.dz * length;
                // A room at the end of it.
                const bool last = step == 3 || (toPortal && step == 1);
                if (toPortal && last && !portal) {
                    // The portal room: lava pool, stairs up to the ring of end portal frames, the
                    // silverfish spawner, iron bars in the windows.
                    const int w = 11, d = 16;
                    const int rx = alongX ? (e.dx > 0 ? e.x : e.x - d + 1) : e.x - w / 2;
                    const int rz = alongX ? e.z - w / 2 : (e.dz > 0 ? e.z : e.z - d + 1);
                    Builder b("code/stronghold_portal_room", alongX ? d : w, 8, alongX ? w : d);
                    const int SX = alongX ? d : w, SZ = alongX ? w : d;
                    for (int xx = 0; xx < SX; xx++)
                        for (int yy = 0; yy < 8; yy++)
                            for (int zz = 0; zz < SZ; zz++) {
                                const bool edge = xx == 0 || xx == SX - 1 || yy == 0 || yy == 7 || zz == 0 || zz == SZ - 1;
                                b.Set(xx, yy, zz, edge ? StoneBrick(r) : "minecraft:air");
                            }
                    const int mx = SX / 2, mz = SZ / 2;
                    b.Fill(mx - 1, 1, mz - 1, mx + 3, 1, mz + 3, "minecraft:stone_bricks");
                    b.Fill(mx, 1, mz, mx + 2, 1, mz + 2, "minecraft:lava");
                    // The twelve frames around the 3x3 portal, facing in; each has an eye one time in ten.
                    for (int k = 0; k < 3; k++) {
                        const auto frame = [&](int fx, int fz, const char* facing) {
                            b.Set(fx, 2, fz, std::string("minecraft:end_portal_frame[eye=") + (r.NextFloat() < 0.1f ? "true" : "false") +
                                                 ",facing=" + facing + "]");
                        };
                        frame(mx + k, mz - 1, "south");
                        frame(mx + k, mz + 3, "north");
                        frame(mx - 1, mz + k, "east");
                        frame(mx + 3, mz + k, "west");
                    }
                    b.Fill(mx, 2, mz, mx + 2, 2, mz + 2, "minecraft:air");
                    const int sxp = alongX ? (e.dx > 0 ? 2 : SX - 3) : mx + 1, szp = alongX ? mz + 1 : (e.dz > 0 ? 2 : SZ - 3);
                    b.Spawner(sxp, 1, szp, "minecraft:silverfish");
                    if (alongX)
                        b.Fill(e.dx > 0 ? 0 : SX - 1, 1, SZ / 2 - 1, e.dx > 0 ? 0 : SX - 1, 3, SZ / 2 + 1, "minecraft:air");
                    else
                        b.Fill(SX / 2 - 1, 1, e.dz > 0 ? 0 : SZ - 1, SX / 2 + 1, 3, e.dz > 0 ? 0 : SZ - 1, "minecraft:air");
                    for (int yy = 3; yy <= 5; yy++)
                        b.Set(0, yy, SZ / 2, "minecraft:iron_bars");
                    AddPiece(start, b.Done(), 0, rx, y, rz);
                    portal = true;
                    pieces++;
                    break;
                }
                const int kind = r.NextInt(4);
                const int w = kind == 0 ? 14 : 9;
                const int rx = alongX ? (e.dx > 0 ? e.x : e.x - w + 1) : e.x - w / 2;
                const int rz = alongX ? e.z - w / 2 : (e.dz > 0 ? e.z : e.z - w + 1);
                const Box roomBox{rx, y, rz, rx + w - 1, y + (kind == 0 ? 10 : 6), rz + w - 1};
                if (Clashes(start, roomBox))
                    break;
                AddPiece(start, StrongholdBox(r, kind == 0 ? "code/stronghold_library" : "code/stronghold_room", w, kind == 0 ? 11 : 7, w,
                                              [&](Builder& b) {
                                                  const int c = w / 2;
                                                  // Doorways on all four sides.
                                                  b.Fill(c - 1, 1, 0, c + 1, 3, 0, "minecraft:air");
                                                  b.Fill(c - 1, 1, w - 1, c + 1, 3, w - 1, "minecraft:air");
                                                  b.Fill(0, 1, c - 1, 0, 3, c + 1, "minecraft:air");
                                                  b.Fill(w - 1, 1, c - 1, w - 1, 3, c + 1, "minecraft:air");
                                                  if (kind == 0) {
                                                      // The library: shelves on the walls, a gallery, two chests.
                                                      for (int yy = 1; yy <= 9; yy++)
                                                          for (int i = 1; i < w - 1; i++)
                                                              if (std::abs(i - c) > 1 || yy > 4) {
                                                                  b.Set(i, yy, 1, "minecraft:bookshelf");
                                                                  b.Set(i, yy, w - 2, "minecraft:bookshelf");
                                                                  b.Set(1, yy, i, "minecraft:bookshelf");
                                                                  b.Set(w - 2, yy, i, "minecraft:bookshelf");
                                                              }
                                                      b.Fill(2, 5, 2, w - 3, 5, w - 3, "minecraft:oak_planks");
                                                      b.Fill(3, 5, 3, w - 4, 5, w - 4, "minecraft:air");
                                                      b.Fill(2, 6, 2, w - 3, 6, 2, "minecraft:oak_fence");
                                                      b.Fill(2, 1, 2, 2, 5, 2, "minecraft:ladder[facing=south]");
                                                      b.Chest(3, 1, 3, "south", "chests/stronghold_library");
                                                      b.Chest(w - 4, 6, w - 4, "north", "chests/stronghold_library");
                                                  } else if (kind == 1) {
                                                      // The fountain room.
                                                      b.Fill(c - 1, 1, c - 1, c + 1, 1, c + 1, "minecraft:stone_bricks");
                                                      b.Set(c, 1, c, "minecraft:water");
                                                      b.Fill(c, 2, c, c, 5, c, "minecraft:stone_bricks");
                                                  } else if (kind == 2) {
                                                      // The prison cells.
                                                      for (int i = 1; i < w - 1; i++)
                                                          if (i != c)
                                                              b.Fill(i, 1, 3, i, 3, 3, "minecraft:iron_bars");
                                                      b.Set(2, 1, 3, "minecraft:iron_door[facing=south,half=lower]");
                                                      b.Set(2, 2, 3, "minecraft:iron_door[facing=south,half=upper]");
                                                  } else {
                                                      // A storeroom with a chest.
                                                      b.Chest(2, 1, 2, "south", "chests/stronghold_crossing");
                                                      b.Set(c, 5, c, "minecraft:torch");
                                                  }
                                              }),
                         0, rx, y, rz);
                pieces++;
                e.x = alongX ? (e.dx > 0 ? rx + w : rx - 1) : rx + w / 2;
                e.z = alongX ? rz + w / 2 : (e.dz > 0 ? rz + w : rz - 1);
                if (r.NextInt(3) == 0) // turn
                    std::swap(e.dx, e.dz);
            }
        }
        if (!portal) {
            // No branch had room: the portal room goes under the crossing.
            Builder b("code/stronghold_portal_room", 11, 8, 11);
            b.Hollow(0, 0, 0, 10, 7, 10, "minecraft:stone_bricks");
            for (int k = 0; k < 3; k++) {
                b.Set(4 + k, 2, 3, "minecraft:end_portal_frame[eye=false,facing=south]");
                b.Set(4 + k, 2, 7, "minecraft:end_portal_frame[eye=false,facing=north]");
                b.Set(3, 2, 4 + k, "minecraft:end_portal_frame[eye=false,facing=east]");
                b.Set(7, 2, 4 + k, "minecraft:end_portal_frame[eye=false,facing=west]");
            }
            b.Fill(4, 1, 4, 6, 1, 6, "minecraft:lava");
            b.Spawner(2, 1, 2, "minecraft:silverfish");
            b.Fill(5, 7, 5, 5, 7, 5, "minecraft:air");
            AddPiece(start, b.Done(), 0, cx - 5, std::max(probe.minY + 2, y - 9), cz - 5);
        }
        return true;
    }

    // --- Nether fortress ----------------------------------------------------------------------
    bool BuildFortress(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int cx = chunkX * 16 + 2, cz = chunkZ * 16 + 2;
        const int y = 48 + r.NextInt(22);
        (void)probe;
        const auto bridge = [&](int x, int z, bool alongX, int length) {
            const int sx = alongX ? length : 5, sz = alongX ? 5 : length;
            const Box box{x, y - 10, z, x + sx - 1, y + 5, z + sz - 1};
            if (Clashes(start, box))
                return false;
            Builder b("code/fortress_bridge", sx, 16, sz);
            for (int i = 0; i < length; i++)
                for (int w = 0; w < 5; w++) {
                    const int lx = alongX ? i : w, lz = alongX ? w : i;
                    b.Set(lx, 10, lz, "minecraft:nether_bricks");
                    for (int h = 11; h <= 14; h++)
                        b.Set(lx, h, lz, "minecraft:air");
                    if (w == 0 || w == 4)
                        b.Set(lx, 11, lz, i % 4 == 2 ? "minecraft:nether_brick_fence[east=true,west=true,north=true,south=true]" : "minecraft:nether_bricks");
                }
            // Pillars down into the lava.
            for (int i = 2; i < length; i += 6)
                for (int h = 0; h < 10; h++)
                    for (int w = 1; w <= 3; w++)
                        b.Set(alongX ? i : w, h, alongX ? w : i, "minecraft:nether_bricks");
            AddPiece(start, b.Done(), 0, x, y - 10, z);
            return true;
        };
        const auto room = [&](int x, int z, int kind) {
            const Box box{x, y, z, x + 12, y + 8, z + 12};
            if (Clashes(start, box))
                return false;
            Builder b(kind == 0 ? "code/fortress_throne" : kind == 1 ? "code/fortress_wart_room" : "code/fortress_room", 13, 9, 13);
            b.Hollow(0, 0, 0, 12, 8, 12, "minecraft:nether_bricks");
            for (int i = 4; i <= 8; i++) {
                b.Set(i, 1, 0, "minecraft:air");
                b.Set(i, 2, 0, "minecraft:air");
                b.Set(i, 1, 12, "minecraft:air");
                b.Set(i, 2, 12, "minecraft:air");
                b.Set(0, 1, i, "minecraft:air");
                b.Set(0, 2, i, "minecraft:air");
                b.Set(12, 1, i, "minecraft:air");
                b.Set(12, 2, i, "minecraft:air");
            }
            for (int i = 2; i <= 10; i += 2)
                for (const int w : {0, 12}) {
                    b.Set(i, 4, w, "minecraft:nether_brick_fence[east=true,west=true]");
                    b.Set(w, 4, i, "minecraft:nether_brick_fence[north=true,south=true]");
                }
            if (kind == 0) {
                // The blaze spawner on its stand.
                b.Fill(5, 1, 5, 7, 1, 7, "minecraft:nether_bricks");
                b.Spawner(6, 2, 6, "minecraft:blaze");
                b.Mob(4, 1, 4, "minecraft:blaze");
            } else if (kind == 1) {
                // Nether wart on soul sand, stairs around it.
                b.Fill(3, 1, 3, 9, 1, 9, "minecraft:soul_sand");
                b.Fill(3, 2, 3, 9, 2, 9, "minecraft:nether_wart[age=3]");
                b.Fill(2, 1, 2, 10, 1, 2, "minecraft:nether_brick_stairs[facing=south]");
            } else {
                b.Chest(2, 1, 2, "south", "chests/nether_bridge");
                b.Mob(6, 1, 6, "minecraft:wither_skeleton");
            }
            AddPiece(start, b.Done(), 0, x, y, z);
            return true;
        };
        room(cx, cz, 0);
        int made = 0;
        struct Arm {
            int x, z, dx, dz, depth;
        };
        std::vector<Arm> arms = {{cx + 13, cz + 4, 1, 0, 0}, {cx - 1, cz + 4, -1, 0, 0}, {cx + 4, cz + 13, 0, 1, 0}, {cx + 4, cz - 1, 0, -1, 0}};
        while (!arms.empty() && made < 24) {
            const Arm a = arms.back();
            arms.pop_back();
            if (a.depth > 4)
                continue;
            const int length = 10 + r.NextInt(12);
            const bool alongX = a.dx != 0;
            const int bx = a.dx < 0 ? a.x - length + 1 : a.x, bz = a.dz < 0 ? a.z - length + 1 : a.z;
            if (!bridge(bx, bz, alongX, length))
                continue;
            made++;
            const int ex = a.x + a.dx * length, ez = a.z + a.dz * length;
            const int rx = alongX ? (a.dx > 0 ? ex : ex - 12) : ex - 4, rz = alongX ? ez - 4 : (a.dz > 0 ? ez : ez - 12);
            if (room(rx, rz, r.NextInt(3))) {
                made++;
                if (alongX) {
                    arms.push_back({a.dx > 0 ? rx + 13 : rx - 1, rz + 4, a.dx, 0, a.depth + 1});
                    if (r.NextInt(2))
                        arms.push_back({rx + 4, rz + 13, 0, 1, a.depth + 1});
                } else {
                    arms.push_back({rx + 4, a.dz > 0 ? rz + 13 : rz - 1, 0, a.dz, a.depth + 1});
                    if (r.NextInt(2))
                        arms.push_back({rx + 13, rz + 4, 1, 0, a.depth + 1});
                }
            }
        }
        return true;
    }

    // --- Ocean monument -------------------------------------------------------------------------
    bool BuildMonument(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start) {
        const int x = chunkX * 16 - 29 + 8, z = chunkZ * 16 - 29 + 8;
        if (!BiomeIn(probe, x + 29, 50, z + 29, {"deep_ocean", "deep_cold_ocean", "deep_lukewarm_ocean", "deep_frozen_ocean"}))
            return false;
        Builder b("code/monument", 58, 23, 58);
        const auto prism = [&] {
            const float f = r.NextFloat();
            return f < 0.5f ? "minecraft:prismarine_bricks" : f < 0.8f ? "minecraft:prismarine" : "minecraft:dark_prismarine";
        };
        // Base slab, outer walls with the two front towers and wings, the roof, water inside.
        b.Fill(0, 0, 0, 57, 22, 57, "minecraft:water");
        for (int xx = 0; xx < 58; xx++)
            for (int zz = 0; zz < 58; zz++)
                b.Set(xx, 0, zz, prism());
        for (int yy = 1; yy <= 12; yy++)
            for (int i = 0; i < 58; i++)
                for (const int w : {0, 57}) {
                    b.Set(i, yy, w, prism());
                    b.Set(w, yy, i, prism());
                }
        b.Fill(23, 1, 0, 34, 9, 0, "minecraft:water"); // the entrance
        for (int k = 0; k < 10; k++)
            b.Fill(k + 1, 13 + k / 2, k + 1, 56 - k, 13 + k / 2, 56 - k, prism());
        b.Fill(10, 13, 10, 47, 17, 47, "minecraft:water");
        // Inner rooms and pillars with sea lanterns.
        for (int px = 8; px < 50; px += 10)
            for (int pz = 8; pz < 50; pz += 10) {
                b.Fill(px, 1, pz, px + 1, 12, pz + 1, prism());
                b.Set(px, 6, pz, "minecraft:sea_lantern");
            }
        b.Fill(24, 1, 24, 33, 8, 33, "minecraft:prismarine_bricks");
        b.Fill(25, 2, 25, 32, 7, 32, "minecraft:water");
        // The treasure: eight gold blocks in the core, wrapped in dark prismarine.
        b.Fill(27, 3, 27, 30, 6, 30, "minecraft:dark_prismarine");
        b.Fill(28, 4, 28, 29, 5, 29, "minecraft:gold_block");
        // Sponge rooms.
        b.Fill(5, 15, 5, 9, 18, 9, "minecraft:wet_sponge");
        b.Fill(6, 16, 6, 8, 17, 8, "minecraft:water");
        // Three elder guardians (the core, both wings) and the guardians.
        b.Mob(28, 9, 28, "minecraft:elder_guardian");
        b.Mob(5, 3, 50, "minecraft:elder_guardian");
        b.Mob(50, 3, 50, "minecraft:elder_guardian");
        for (int i = 0; i < 6; i++)
            b.Mob(10 + r.NextInt(38), 2 + r.NextInt(10), 10 + r.NextInt(38), "minecraft:guardian");
        AddPiece(start, b.Done(), 0, x, 39, z);
        return true;
    }

    // --- End city -------------------------------------------------------------------------------
    bool BuildEndCity(WorldRandom& r, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, Start& start) {
        if (!store)
            return false;
        const int x = chunkX * 16 + 7, z = chunkZ * 16 + 7;
        const int ground = probe.surface(x, z);
        if (ground < 60 || !BiomeIn(probe, x, ground, z, {"end_highlands", "end_midlands"}))
            return false; // only on the outer islands, at least that high
        const int turns = r.NextInt(4);
        int y = ground;
        // The base, its floors, then a tower on top; sometimes a ship floating beside it.
        for (const char* name : {"endcity/base_floor", "endcity/second_floor", "endcity/third_floor", "endcity/third_roof"}) {
            const auto t = store->Get(name);
            if (!t)
                return !start.pieces.empty();
            AddPiece(start, t, turns, x, y, z);
            y += t->sy - (std::string_view(name).ends_with("roof") ? 0 : 1);
        }
        if (const auto base = store->Get("endcity/tower_base")) {
            AddPiece(start, base, turns, x + 2, y, z + 2);
            y += base->sy;
            const int floors = 2 + r.NextInt(3);
            for (int i = 0; i < floors; i++)
                if (const auto piece = store->Get("endcity/tower_piece")) {
                    AddPiece(start, piece, turns, x + 2, y, z + 2);
                    y += piece->sy;
                }
            if (const auto top = store->Get("endcity/tower_top"))
                AddPiece(start, top, turns, x + 1, y, z + 1);
        }
        if (r.NextInt(2) == 0)
            if (const auto ship = store->Get("endcity/ship")) {
                const Box box{x + 20, ground + 10, z - 5, x + 20 + ship->sx, ground + 10 + ship->sy, z - 5 + ship->sz};
                if (!Clashes(start, box))
                    AddPiece(start, ship, 0, x + 20, ground + 10, z - 5);
            }
        return true;
    }

    // --- Woodland mansion ---------------------------------------------------------------------
    bool BuildMansion(WorldRandom& r, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, Start& start) {
        (void)store;
        const int x = chunkX * 16 + 7, z = chunkZ * 16 + 7;
        const int ground = probe.surface(x + 15, z + 15);
        if (!BiomeIn(probe, x + 15, ground, z + 15, {"dark_forest", "pale_garden"}))
            return false;
        // Two storeys of dark oak and cobblestone with rooms off a central hall, a roof, chests,
        // vindicators and evokers.
        const int W = 31, D = 31, H = 18;
        Builder b("code/mansion", W, H, D);
        b.Fill(0, 0, 0, W - 1, 0, D - 1, "minecraft:cobblestone");
        for (int floor = 0; floor < 2; floor++) {
            const int y0 = 1 + floor * 6;
            b.Hollow(0, y0 - 1, 0, W - 1, y0 + 5, D - 1, "minecraft:dark_oak_planks", "minecraft:air");
            for (int i = 0; i < W; i += 5) {
                b.Fill(i, y0, 0, i, y0 + 4, 0, "minecraft:dark_oak_log[axis=y]");
                b.Fill(i, y0, D - 1, i, y0 + 4, D - 1, "minecraft:dark_oak_log[axis=y]");
                b.Fill(0, y0, i, 0, y0 + 4, i, "minecraft:dark_oak_log[axis=y]");
                b.Fill(W - 1, y0, i, W - 1, y0 + 4, i, "minecraft:dark_oak_log[axis=y]");
                if (i + 2 < W) {
                    b.Set(i + 2, y0 + 2, 0, "minecraft:glass_pane[east=true,west=true]");
                    b.Set(i + 2, y0 + 2, D - 1, "minecraft:glass_pane[east=true,west=true]");
                }
            }
            // Rooms: walls every ten blocks with doorways, carpets, a chest in some.
            for (int w = 10; w < W - 1; w += 10) {
                b.Fill(w, y0, 1, w, y0 + 4, D - 2, "minecraft:birch_planks");
                b.Fill(1, y0, w, W - 2, y0 + 4, w, "minecraft:birch_planks");
                for (int k = 5; k < W - 1; k += 10) {
                    b.Fill(w, y0, k, w, y0 + 2, k, "minecraft:air");
                    b.Fill(k, y0, w, k, y0 + 2, w, "minecraft:air");
                }
            }
            for (int rx = 0; rx < 3; rx++)
                for (int rz = 0; rz < 3; rz++) {
                    const int ox = 1 + rx * 10, oz = 1 + rz * 10;
                    const char* carpet = Pick(r, {"minecraft:red_carpet", "minecraft:white_carpet", "minecraft:brown_carpet"});
                    b.Fill(ox + 1, y0, oz + 1, ox + 7, y0, oz + 7, carpet);
                    if (r.NextInt(3) == 0)
                        b.Chest(ox + 1, y0, oz + 1, "south", "chests/woodland_mansion");
                    if (r.NextInt(2) == 0)
                        b.Mob(ox + 4, y0, oz + 4, r.NextInt(3) == 0 ? "minecraft:evocation_illager" : "minecraft:vindicator");
                }
        }
        b.Fill(14, 1, 0, 16, 3, 0, "minecraft:air"); // the front door
        b.Fill(14, 0, 0, 16, 0, 0, "minecraft:cobblestone_stairs[facing=south]");
        for (int k = 0; k < 5; k++)
            b.Fill(k, 13 + k, k, W - 1 - k, 13 + k, D - 1 - k, "minecraft:dark_oak_planks");
        b.Fill(5, 17, 5, W - 6, 17, D - 6, "minecraft:dark_oak_slab[type=bottom]");
        AddPiece(start, b.Done(), r.NextInt(4), x, ground, z);
        return true;
    }

    // --- Chunk features -----------------------------------------------------------------------
    namespace {
        struct ChunkView {
            ChunkColumn& out;
            int minY, maxY;
            uint16_t Get(int x, int y, int z) const {
                if (x < 0 || x > 15 || z < 0 || z > 15 || y < minY || y > maxY)
                    return BlockRegistry::kAir;
                return out.Get(x, y, z);
            }
            void Set(int x, int y, int z, uint16_t b) {
                if (x >= 0 && x <= 15 && z >= 0 && z <= 15 && y > minY && y <= maxY)
                    out.Set(x, y, z, b);
            }
        };
        constexpr std::pair<int, int> kSides4[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        constexpr std::pair<int, int> kCorners4[4] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
        uint16_t Bk(const char* name) { return BlockCatalog::Instance().DefaultBlock(name); }
        bool Solid(uint16_t b) {
            if (b == BlockRegistry::kAir)
                return false;
            const std::string n = BlockRegistry::Instance().Name(b);
            return !n.contains("water") && !n.contains("lava") && !n.contains("air");
        }
    }

    void PlaceChunkFeatures(int64_t seed, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, ChunkColumn& out) {
        if (probe.dimension != kOverworld)
            return;
        WorldRandom r = WorldRandom::Legacy(seed ^ (static_cast<int64_t>(chunkX) * 341873128712ll + static_cast<int64_t>(chunkZ) * 132897987541ll) ^
                                            0x5DEECE66Dll);
        ChunkView v{out, probe.minY, probe.maxY};
        const uint16_t air = BlockRegistry::kAir, cobble = Bk("minecraft:cobblestone"), mossy = Bk("minecraft:mossy_cobblestone"),
                       spawner = Bk("minecraft:mob_spawner"), chest = Bk("minecraft:chest");
        // Dungeons: rooms of cobblestone with a spawner and up to two chests, where the cave
        // around has between one and five openings at floor level (the game's rule).
        for (int attempt = 0; attempt < 8; attempt++) {
            const int cx = 4 + r.NextInt(8), cz = 4 + r.NextInt(8), cy = probe.minY + 6 + r.NextInt(100);
            const int xr = r.NextInt(2) + 2, zr = r.NextInt(2) + 2;
            int openings = 0;
            bool ok = true;
            for (int dx = -xr - 1; dx <= xr + 1 && ok; dx++)
                for (int dz = -zr - 1; dz <= zr + 1 && ok; dz++)
                    for (int dy = -1; dy <= 4; dy++) {
                        const uint16_t b = v.Get(cx + dx, cy + dy, cz + dz);
                        if ((dy == -1 || dy == 4) && !Solid(b)) {
                            ok = false;
                            break;
                        }
                        if ((std::abs(dx) == xr + 1 || std::abs(dz) == zr + 1) && dy == 0 && b == air && v.Get(cx + dx, cy + 1, cz + dz) == air)
                            openings++;
                    }
            if (!ok || openings < 1 || openings > 5)
                continue;
            for (int dx = -xr - 1; dx <= xr + 1; dx++)
                for (int dz = -zr - 1; dz <= zr + 1; dz++)
                    for (int dy = 3; dy >= -1; dy--) {
                        const bool wall = std::abs(dx) == xr + 1 || std::abs(dz) == zr + 1 || dy == -1;
                        if (!wall) {
                            v.Set(cx + dx, cy + dy, cz + dz, air);
                        } else if (Solid(v.Get(cx + dx, cy + dy, cz + dz))) {
                            v.Set(cx + dx, cy + dy, cz + dz, dy == -1 && r.NextInt(4) != 0 ? mossy : cobble);
                        }
                    }
            for (int c = 0; c < 2; c++)
                for (int tries = 0; tries < 3; tries++) {
                    const int qx = cx + r.NextInt(xr * 2 + 1) - xr, qz = cz + r.NextInt(zr * 2 + 1) - zr;
                    int walls = 0;
                    for (const auto& d : kSides4)
                        walls += Solid(v.Get(qx + d.first, cy, qz + d.second));
                    if (walls == 1 && v.Get(qx, cy, qz) == air) {
                        v.Set(qx, cy, qz, chest);
                        out.generated.push_back({GeneratedExtra::Loot, chunkX * 16 + qx, cy, chunkZ * 16 + qz, "chests/simple_dungeon"});
                        break;
                    }
                }
            v.Set(cx, cy, cz, spawner);
            break;
        }
        // Desert wells: one chunk in a thousand of desert.
        if (r.NextInt(1000) == 0) {
            const int wx = 8, wz = 8;
            const int top = probe.surface(chunkX * 16 + wx, chunkZ * 16 + wz);
            if (BiomeIn(probe, chunkX * 16 + wx, top, chunkZ * 16 + wz, {"desert"})) {
                const uint16_t sandstone = Bk("minecraft:sandstone"), water = Bk("minecraft:water"), slab = Bk("minecraft:sandstone_slab");
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++) {
                        v.Set(wx + dx, top - 1, wz + dz, sandstone);
                        v.Set(wx + dx, top - 2, wz + dz, sandstone);
                        if (std::abs(dx) == 2 || std::abs(dz) == 2)
                            v.Set(wx + dx, top, wz + dz, sandstone);
                    }
                v.Set(wx, top - 1, wz, water);
                for (const auto& d : kSides4)
                    v.Set(wx + d.first, top - 1, wz + d.second, water);
                for (const auto& d : kCorners4)
                    for (int h = 0; h < 3; h++)
                        v.Set(wx + d.first, top + h, wz + d.second, sandstone);
                for (int dx = -1; dx <= 1; dx++)
                    for (int dz = -1; dz <= 1; dz++)
                        v.Set(wx + dx, top + 3, wz + dz, dx == 0 && dz == 0 ? sandstone : slab);
            }
        }
        // Fossils: bones in the ground of deserts and swamps, one chunk in 64.
        if (store && r.NextInt(64) == 0) {
            const int fx = chunkX * 16 + 8, fz = chunkZ * 16 + 8;
            const int top = probe.surface(fx, fz);
            if (BiomeIn(probe, fx, top, fz, {"desert", "swamp", "mangrove_swamp"})) {
                const auto& pool = store->Pool("fossils");
                if (!pool.empty())
                    if (const auto t = store->Get(pool[static_cast<size_t>(r.NextInt(static_cast<int>(pool.size())))])) {
                        const int turns = r.NextInt(4), y = top - 15 - r.NextInt(10);
                        const auto& palette = t->Bedrock(turns);
                        for (const TemplateBlock& b : t->blocks) {
                            if (t->isAir[b.state] || palette[b.state] == air || r.NextFloat() > 0.9f)
                                continue;
                            int rx, rz;
                            Turned(turns, b.x, b.z, rx, rz);
                            v.Set(8 - t->sx / 2 + rx, y + b.y, 8 - t->sz / 2 + rz, palette[b.state]);
                        }
                    }
            }
        }
        // Amethyst geodes: one chunk in 24, deep down.
        if (r.NextInt(24) == 0) {
            const int gy = probe.minY + 6 + r.NextInt(80);
            const double radius = 3.5 + r.NextDouble() * 1.5;
            const uint16_t basalt = Bk("minecraft:smooth_basalt"), calcite = Bk("minecraft:calcite"), amethyst = Bk("minecraft:amethyst_block"),
                           budding = Bk("minecraft:budding_amethyst");
            if (Solid(v.Get(8, gy, 8)))
                for (int dx = -6; dx <= 6; dx++)
                    for (int dy = -6; dy <= 6; dy++)
                        for (int dz = -6; dz <= 6; dz++) {
                            const double d = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
                            if (d > radius + 2.0)
                                continue;
                            uint16_t b;
                            if (d > radius + 1.0)
                                b = basalt;
                            else if (d > radius)
                                b = calcite;
                            else if (d > radius - 1.0)
                                b = r.NextFloat() < 0.083f ? budding : amethyst;
                            else
                                b = air;
                            v.Set(8 + dx, gy + dy, 8 + dz, b);
                        }
        }
    }
}
