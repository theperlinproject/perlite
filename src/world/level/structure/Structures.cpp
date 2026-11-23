// Structure generation. Where structures start follows the game's placement grids (spacing,
// separation and salt per structure set, the start chunk drawn with a random seeded from the region),
// the jigsaw assembly follows the game's algorithm (pieces pulled from pools, shuffled, tried in every
// rotation, kept when they fit the free space left by the others), and the pieces are written into
// each chunk as it generates. The templates and pools come from the Bedrock vanilla pack (see
// Templates.hpp); the numbers below are the game's.
#include "world/level/structure/Structures.hpp"

#include "world/level/structure/CodeStructures.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"
#include "world/level/levelgen/vanilla/JavaRandom.hpp"

#include <random>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <deque>
#include <set>
#include <string_view>

namespace perlite::structure {
    using vanilla::WorldRandom;

    void Box::Encapsulate(const Box& o) {
        x0 = std::min(x0, o.x0);
        y0 = std::min(y0, o.y0);
        z0 = std::min(z0, o.z0);
        x1 = std::max(x1, o.x1);
        y1 = std::max(y1, o.y1);
        z1 = std::max(z1, o.z1);
    }

    enum class Kind { Jigsaw, Single, Outpost, Code };

    struct StructureGenerator::Def {
        const char* name;
        Kind kind;
        std::vector<std::string> startPool; // a pool folder, or template names
        const char* startJigsaw = nullptr;
        int depth = 0;
        int startY = 0;
        bool project = false;       // start height is measured from the surface
        int maxDistance = 80;
        bool expansion = false;
        std::vector<std::string_view> biomes;
        int adaptation = 0;
        int weight = 1;
        // Kind::Code: builds the pieces (CodeStructures.hpp).
        std::function<bool(vanilla::WorldRandom&, const WorldProbe&, const TemplateStore*, int, int, Start&)> build;
    };

    struct StructureGenerator::Set {
        const char* name;
        int dimension;
        int spacing, separation, salt;
        bool triangular = false;
        float frequency = 1.0f;
        int frequencyMethod = 0; // 0 default, 1 outposts, 2 buried treasure, 3 mineshafts (the game's legacy rules)
        const Set* exclusion = nullptr;
        int exclusionChunks = 0;
        std::vector<Def> structures;
        // Bedrock's spread: the region's start drawn with a Mersenne Twister seeded from the region
        // (the same formula as the large-feature seed, truncated to 32 bits).
        bool bedrock = false;
    };

    namespace {
        using Def = StructureGenerator::Def;
        using Set = StructureGenerator::Set;

        const std::vector<Set>& Sets() {
            static const std::vector<Set> sets = [] {
                std::vector<Set> s;
                const std::vector<std::string_view> outpostBiomes = {"desert", "plains", "savanna", "snowy_plains", "taiga", "meadow",
                                                                     "frozen_peaks", "jagged_peaks", "stony_peaks", "snowy_slopes",
                                                                     "grove", "cherry_grove"};
                const auto village = [](const char* name, const char* type, std::vector<std::string_view> biomes) {
                    Def d{name, Kind::Jigsaw, {std::string("village/") + type + "/town_centers"}};
                    d.depth = 6;
                    d.project = true;
                    d.expansion = true;
                    d.biomes = std::move(biomes);
                    d.adaptation = 1;
                    return d;
                };
                s.push_back({"villages", kOverworld, 34, 8, 10387312, false, 1.0f, 0, nullptr, 0,
                             {village("village_plains", "plains", {"plains", "meadow"}), village("village_desert", "desert", {"desert"}),
                              village("village_savanna", "savanna", {"savanna"}),
                              village("village_snowy", "snowy", {"snowy_plains"}), village("village_taiga", "taiga", {"taiga"})}});
                {
                    Def d{"pillager_outpost", Kind::Outpost, {"pillageroutpost/watchtower"}};
                    d.biomes = outpostBiomes;
                    d.adaptation = 1;
                    s.push_back({"pillager_outposts", kOverworld, 32, 8, 165745296, false, 0.2f, 1, nullptr, 10, {d}});
                }
                {
                    Def d{"ancient_city", Kind::Jigsaw, {"ancient_city/city_center"}, "minecraft:city_anchor"};
                    d.depth = 7;
                    d.startY = -27;
                    d.maxDistance = 116;
                    d.biomes = {"deep_dark"};
                    d.adaptation = 1;
                    s.push_back({"ancient_cities", kOverworld, 24, 8, 20083232, false, 1.0f, 0, nullptr, 0, {d}});
                }
                {
                    Def d{"trail_ruins", Kind::Jigsaw, {"trail_ruins/tower"}};
                    d.depth = 7;
                    d.startY = -15;
                    d.project = true;
                    d.biomes = {"taiga", "snowy_taiga", "old_growth_pine_taiga", "old_growth_spruce_taiga", "old_growth_birch_forest", "jungle"};
                    d.adaptation = 2;
                    s.push_back({"trail_ruins", kOverworld, 34, 8, 83469867, false, 1.0f, 0, nullptr, 0, {d}});
                }
                {
                    Def d{"igloo", Kind::Single, {"igloo/igloo_top_trapdoor", "igloo/igloo_top_no_trapdoor"}};
                    d.biomes = {"snowy_taiga", "snowy_plains", "snowy_slopes"};
                    s.push_back({"igloos", kOverworld, 32, 8, 14357618, false, 1.0f, 0, nullptr, 0, {d}});
                }
                {
                    Def d{"shipwreck", Kind::Single, {"shipwreck"}};
                    d.biomes = {"ocean", "deep_ocean", "cold_ocean", "deep_cold_ocean", "lukewarm_ocean", "deep_lukewarm_ocean",
                                "warm_ocean", "frozen_ocean", "deep_frozen_ocean", "beach", "snowy_beach"};
                    s.push_back({"shipwrecks", kOverworld, 24, 4, 165745295, false, 1.0f, 0, nullptr, 0, {d}});
                }
                {
                    Def d{"ocean_ruin", Kind::Single, {"ruin"}};
                    d.biomes = {"ocean", "deep_ocean", "cold_ocean", "deep_cold_ocean", "lukewarm_ocean", "deep_lukewarm_ocean",
                                "warm_ocean", "frozen_ocean", "deep_frozen_ocean"};
                    s.push_back({"ocean_ruins", kOverworld, 20, 8, 14357621, false, 1.0f, 0, nullptr, 0, {d}});
                }
                {
                    Def d{"ruined_portal", Kind::Single, {"ruined_portal"}};
                    s.push_back({"ruined_portals", kOverworld, 40, 15, 34222645, false, 1.0f, 0, nullptr, 0, {d}});
                    Def n = d;
                    n.name = "ruined_portal_nether";
                    s.push_back({"ruined_portals_nether", kNether, 40, 15, 34222645, false, 1.0f, 0, nullptr, 0, {n}});
                }
                {
                    Def d{"bastion_remnant", Kind::Jigsaw,
                          {"bastion/units/air_base", "bastion/hoglin_stable/air_base", "bastion/treasure/big_air_full",
                           "bastion/bridge/starting_pieces/entrance_base"}};
                    d.depth = 6;
                    d.startY = 33;
                    d.biomes = {"nether_wastes", "soul_sand_valley", "warped_forest", "crimson_forest"};
                    d.weight = 3;
                    // The nether complexes set holds the fortress too (weight 2), which is built in code
                    // and not generated yet: its share of the starts stays empty.
                    Def fortress{"fortress", Kind::Code, {}};
                    fortress.weight = 2;
                    fortress.biomes = {"nether_wastes", "soul_sand_valley", "warped_forest", "crimson_forest", "basalt_deltas"};
                    fortress.build = [](vanilla::WorldRandom& r, const WorldProbe& p, const TemplateStore*, int x, int z, Start& st) {
                        return BuildFortress(r, p, x, z, st);
                    };
                    s.push_back({"nether_complexes", kNether, 27, 4, 30084232, false, 1.0f, 0, nullptr, 0, {d, fortress}});
                }
                const auto code = [](const char* name, auto fn) {
                    Def d{name, Kind::Code, {}};
                    d.build = [fn](vanilla::WorldRandom& r, const WorldProbe& p, const TemplateStore* st, int x, int z, Start& out) {
                        return fn(r, p, st, x, z, out);
                    };
                    return d;
                };
                s.push_back({"desert_pyramids", kOverworld, 32, 8, 14357617, false, 1.0f, 0, nullptr, 0,
                             {code("desert_pyramid", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildDesertPyramid(r, p, x, z, o); })}});
                s.push_back({"jungle_temples", kOverworld, 32, 8, 14357619, false, 1.0f, 0, nullptr, 0,
                             {code("jungle_pyramid", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildJunglePyramid(r, p, x, z, o); })}});
                s.push_back({"swamp_huts", kOverworld, 32, 8, 14357620, false, 1.0f, 0, nullptr, 0,
                             {code("swamp_hut", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildSwampHut(r, p, x, z, o); })}});
                s.push_back({"buried_treasures", kOverworld, 1, 0, 0, false, 0.01f, 2, nullptr, 0,
                             {code("buried_treasure", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildBuriedTreasure(r, p, x, z, o); })}});
                s.push_back({"mineshafts", kOverworld, 1, 0, 0, false, 0.004f, 3, nullptr, 0,
                             {code("mineshaft", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildMineshaft(r, p, x, z, o); })}});
                s.push_back({"ocean_monuments", kOverworld, 32, 5, 10387313, true, 1.0f, 0, nullptr, 0,
                             {code("monument", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildMonument(r, p, x, z, o); })}});
                s.push_back({"woodland_mansions", kOverworld, 80, 20, 10387319, true, 1.0f, 0, nullptr, 0,
                             {code("mansion", [](auto& r, auto& p, auto st, int x, int z, Start& o) { return BuildMansion(r, p, st, x, z, o); })}});
                // Strongholds: on the game's rings (spacing 0 marks them).
                s.push_back({"strongholds", kOverworld, 0, 0, 0, false, 1.0f, 0, nullptr, 0,
                             {code("stronghold", [](auto& r, auto& p, auto, int x, int z, Start& o) { return BuildStronghold(r, p, x, z, o); })}});
                s.push_back({"end_cities", kEnd, 20, 11, 10387313, true, 1.0f, 0, nullptr, 0,
                             {code("end_city", [](auto& r, auto& p, auto st, int x, int z, Start& o) { return BuildEndCity(r, p, st, x, z, o); })}});
                {
                    Def d{"nether_fossil", Kind::Single, {"nether_fossils"}};
                    d.biomes = {"soul_sand_valley"};
                    s.push_back({"nether_fossils", kNether, 2, 1, 14357921, false, 1.0f, 0, nullptr, 0, {d}});
                }
                // Where Bedrock places a set differently from the Java game, its own spread. These
                // were measured against the Bedrock Dedicated Server's /locate (seed 12345, twenty
                // places a type): every located start falls on the start these give.
                const auto bedrock = [&](const char* name, int spacing, int separation, int salt, bool triangular) {
                    for (Set& set : s)
                        if (std::string_view(set.name) == name) {
                            set.spacing = spacing;
                            set.separation = separation;
                            set.salt = salt;
                            set.triangular = triangular;
                            set.frequency = 1.0f;
                            set.frequencyMethod = 0;
                            set.bedrock = true;
                        }
                };
                bedrock("villages", 34, 8, 10387312, true);
                bedrock("pillager_outposts", 80, 24, 165745296, true);
                bedrock("shipwrecks", 24, 4, 165745295, false);
                bedrock("ocean_monuments", 32, 5, 10387313, true);
                bedrock("woodland_mansions", 80, 20, 10387319, true);
                bedrock("ancient_cities", 24, 8, 20083232, true);
                bedrock("ocean_ruins", 20, 8, 14357621, false);
                bedrock("buried_treasures", 4, 2, 16842397, true);
                bedrock("ruined_portals", 40, 15, 40552231, false);
                // Desert pyramids, jungle temples, swamp huts and igloos are one set in Bedrock (its
                // "temple"): the start's biome decides which is built.
                {
                    Set temples{"temples", kOverworld, 32, 8, 14357617, false, 1.0f, 0, nullptr, 0, {}};
                    temples.bedrock = true;
                    for (const char* name : {"desert_pyramids", "jungle_temples", "swamp_huts", "igloos"})
                        for (auto it = s.begin(); it != s.end(); ++it)
                            if (std::string_view(it->name) == name) {
                                for (Def& d : it->structures)
                                    temples.structures.push_back(std::move(d));
                                s.erase(it);
                                break;
                            }
                    s.push_back(std::move(temples));
                }
                return s;
            }();
            return sets;
        }

        const Set* SetNamed(std::string_view name) {
            for (const Set& s : Sets())
                if (name == s.name)
                    return &s;
            return nullptr;
        }

        // WorldgenRandom.setLargeFeatureSeed and setLargeFeatureWithSalt.
        WorldRandom LargeFeature(int64_t seed, int chunkX, int chunkZ) {
            WorldRandom r = WorldRandom::Legacy(seed);
            const int64_t a = static_cast<int64_t>(r.NextLong()), b = static_cast<int64_t>(r.NextLong());
            return WorldRandom::Legacy(static_cast<int64_t>((static_cast<uint64_t>(chunkX) * static_cast<uint64_t>(a)) ^
                                                            (static_cast<uint64_t>(chunkZ) * static_cast<uint64_t>(b)) ^
                                                            static_cast<uint64_t>(seed)));
        }
        WorldRandom WithSalt(int64_t seed, int regionX, int regionZ, int salt) {
            return WorldRandom::Legacy(static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(regionX)) * 341873128712ull +
                                                            static_cast<uint64_t>(static_cast<int64_t>(regionZ)) * 132897987541ull +
                                                            static_cast<uint64_t>(seed) + static_cast<uint64_t>(static_cast<int64_t>(salt))));
        }

        int FloorDiv(int a, int b) { return static_cast<int>(std::floor(static_cast<double>(a) / b)); }

        // The start chunk of the region a chunk is in (the game's random spread placement).
        std::pair<int, int> PotentialStart(const Set& set, int64_t seed, int chunkX, int chunkZ) {
            const int rx = FloorDiv(chunkX, set.spacing), rz = FloorDiv(chunkZ, set.spacing);
            if (set.bedrock) {
                const uint64_t s = static_cast<uint64_t>(static_cast<int64_t>(rx)) * 341873128712ull +
                                   static_cast<uint64_t>(static_cast<int64_t>(rz)) * 132897987541ull + static_cast<uint64_t>(seed) +
                                   static_cast<uint64_t>(static_cast<int64_t>(set.salt));
                std::mt19937 mt(static_cast<uint32_t>(s));
                const uint32_t k = static_cast<uint32_t>(set.spacing - set.separation);
                const auto spread = [&] {
                    if (!set.triangular)
                        return static_cast<int>(mt() % k);
                    const uint32_t a = mt() % k;
                    const uint32_t b = mt() % k;
                    return static_cast<int>((a + b) / 2);
                };
                const int ox = spread();
                const int oz = spread();
                return {rx * set.spacing + ox, rz * set.spacing + oz};
            }
            WorldRandom r = WithSalt(seed, rx, rz, set.salt);
            const int k = set.spacing - set.separation;
            const auto spread = [&] { return set.triangular ? (r.NextInt(k) + r.NextInt(k)) / 2 : r.NextInt(k); };
            const int ox = spread(), oz = spread();
            return {rx * set.spacing + ox, rz * set.spacing + oz};
        }

        bool PassesFrequency(const Set& set, int64_t seed, int chunkX, int chunkZ) {
            if (set.frequency >= 1.0f)
                return true;
            if (set.frequencyMethod == 2) {
                WorldRandom r = WithSalt(seed, chunkX, chunkZ, 10387320);
                return r.NextFloat() < set.frequency;
            }
            if (set.frequencyMethod == 3) {
                WorldRandom r = LargeFeature(seed, chunkX, chunkZ);
                return r.NextDouble() < set.frequency;
            }
            if (set.frequencyMethod == 1) {
                const int i = chunkX >> 4, j = chunkZ >> 4;
                WorldRandom r = WorldRandom::Legacy(static_cast<int64_t>(i ^ (j << 4)) ^ seed);
                r.NextInt();
                return r.NextInt(static_cast<int>(1.0f / set.frequency)) == 0;
            }
            WorldRandom r = WithSalt(seed, chunkX, chunkZ, set.salt);
            return r.NextFloat() < set.frequency;
        }

        bool IsStartChunk(const Set& set, int64_t seed, int chunkX, int chunkZ) {
            if (set.spacing == 0) {
                for (const auto& [x, z] : StrongholdChunks(seed))
                    if (x == chunkX && z == chunkZ)
                        return true;
                return false;
            }
            const auto [sx, sz] = PotentialStart(set, seed, chunkX, chunkZ);
            return sx == chunkX && sz == chunkZ && PassesFrequency(set, seed, chunkX, chunkZ);
        }

        // Rotation of a template position about its origin (the game's StructureTemplate.transform).
        void Turn(int turns, int x, int z, int& ox, int& oz) {
            switch (turns & 3) {
            case 1:
                ox = -z;
                oz = x;
                break;
            case 2:
                ox = -x;
                oz = -z;
                break;
            case 3:
                ox = z;
                oz = -x;
                break;
            default:
                ox = x;
                oz = z;
            }
        }

        Box BoxOf(const Template& t, int turns, int px, int py, int pz) {
            int ax, az, bx, bz;
            Turn(turns, 0, 0, ax, az);
            Turn(turns, t.sx - 1, t.sz - 1, bx, bz);
            return {std::min(ax, bx) + px, py, std::min(az, bz) + pz, std::max(ax, bx) + px, py + t.sy - 1, std::max(az, bz) + pz};
        }

        std::string TurnDir(const std::string& d, int turns) {
            static const char* const k[4] = {"north", "east", "south", "west"};
            for (int i = 0; i < 4; i++)
                if (d == k[i])
                    return k[(i + turns) & 3];
            return d;
        }
        std::string OppositeDir(const std::string& d) {
            if (d == "north")
                return "south";
            if (d == "south")
                return "north";
            if (d == "east")
                return "west";
            if (d == "west")
                return "east";
            if (d == "up")
                return "down";
            return "up";
        }
        void Step(const std::string& d, int& x, int& y, int& z) {
            if (d == "north")
                z--;
            else if (d == "south")
                z++;
            else if (d == "east")
                x++;
            else if (d == "west")
                x--;
            else if (d == "up")
                y++;
            else if (d == "down")
                y--;
        }

        // A jigsaw of a placed piece: world position and directions.
        struct PlacedJigsaw {
            const Jigsaw* j;
            int x, y, z;
            std::string front, top;
        };

        std::vector<PlacedJigsaw> JigsawsOf(const Template& t, int turns, int px, int py, int pz, WorldRandom& random) {
            std::vector<PlacedJigsaw> list;
            for (const Jigsaw& j : t.jigsaws) {
                int rx, rz;
                Turn(turns, j.x, j.z, rx, rz);
                list.push_back({&j, rx + px, j.y + py, rz + pz, TurnDir(j.front, turns), TurnDir(j.top, turns)});
            }
            // Util.shuffle, then the stable sort by selection priority (highest first).
            for (size_t i = list.size(); i > 1; i--)
                std::swap(list[i - 1], list[static_cast<size_t>(random.NextInt(static_cast<int>(i)))]);
            std::ranges::stable_sort(list, [](const PlacedJigsaw& a, const PlacedJigsaw& b) {
                return a.j->selectionPriority > b.j->selectionPriority;
            });
            return list;
        }

        bool CanAttach(const PlacedJigsaw& from, const PlacedJigsaw& to) {
            return to.front == OppositeDir(from.front) && (from.j->rollable || from.top == to.top) && from.j->target == to.j->name;
        }

        // The free space a piece may grow into: a box with the boxes already used cut out.
        struct Space {
            Box outer;
            std::vector<Box> used;
            bool Fits(const Box& b) const {
                if (!outer.Contains(b))
                    return false;
                for (const Box& u : used)
                    if (u.Intersects(b))
                        return false;
                return true;
            }
        };

        template <class T> void Shuffle(std::vector<T>& v, WorldRandom& r) {
            for (size_t i = v.size(); i > 1; i--)
                std::swap(v[i - 1], v[static_cast<size_t>(r.NextInt(static_cast<int>(i)))]);
        }
    }

    StructureGenerator::StructureGenerator(std::shared_ptr<TemplateStore> s, WorldProbe p) : store(std::move(s)), probe(std::move(p)) {
        for (const Set& set : Sets())
            if (set.dimension == probe.dimension)
                sets.push_back(&set);
    }

    bool StructureGenerator::Assemble(const Def& def, Start& start, int chunkX, int chunkZ) {
        WorldRandom random = LargeFeature(probe.seed, chunkX, chunkZ);
        const int turns = random.NextInt(4); // Rotation.getRandom
        // The start piece: a random template of the start pool.
        std::vector<std::string> startNames;
        for (const std::string& p : def.startPool) {
            const auto& pool = store->Pool(p);
            if (!pool.empty())
                startNames.insert(startNames.end(), pool.begin(), pool.end());
            else if (store->Get(p))
                startNames.push_back(p);
        }
        if (startNames.empty())
            return false;
        const auto tmpl = store->Get(startNames[static_cast<size_t>(random.NextInt(static_cast<int>(startNames.size())))]);
        if (!tmpl)
            return false;
        int px = chunkX * 16, py = def.startY, pz = chunkZ * 16;
        if (def.startJigsaw) {
            // The named jigsaw of the start piece goes at the start position.
            std::vector<PlacedJigsaw> named;
            for (const PlacedJigsaw& j : JigsawsOf(*tmpl, turns, px, py, pz, random))
                if (j.j->name == def.startJigsaw)
                    named.push_back(j);
            if (named.empty())
                return false;
            const PlacedJigsaw& a = named[static_cast<size_t>(random.NextInt(static_cast<int>(named.size())))];
            px -= a.x - px;
            py -= a.y - py;
            pz -= a.z - pz;
        }
        Piece first{tmpl, turns, px, py, pz, BoxOf(*tmpl, turns, px, py, pz), false, std::string_view(def.name).starts_with("village"), 1};
        const int centerX = (first.box.x0 + first.box.x1) / 2, centerZ = (first.box.z0 + first.box.z1) / 2;
        const int y = def.project ? py + probe.surface(centerX, centerZ) : py;
        const int shift = y - (first.box.y0 + first.ground);
        first.y += shift;
        first.box.y0 += shift;
        first.box.y1 += shift;
        // The biome where it starts decides whether it is this structure.
        if (!def.biomes.empty()) {
            const int b = probe.biome(centerX, y, centerZ);
            if (b < 0 || b >= vanilla::kBiomeCount ||
                std::ranges::find(def.biomes, std::string_view(vanilla::kBiomes[b].java)) == def.biomes.end())
                return false;
        }
        start.pieces.push_back(first);
        if (def.depth <= 0)
            return true;

        struct Pending {
            size_t piece;
            std::shared_ptr<Space> space;
            int depth;
        };
        const int maxY = probe.maxY, minY = probe.minY;
        auto outerSpace = std::make_shared<Space>();
        outerSpace->outer = {centerX - def.maxDistance, std::max(y - def.maxDistance, minY + 1), centerZ - def.maxDistance,
                             centerX + def.maxDistance, std::min(y + def.maxDistance, maxY), centerZ + def.maxDistance};
        outerSpace->used.push_back(first.box);
        std::deque<Pending> queue{{0, outerSpace, 0}};
        std::map<std::pair<int, int>, int> surfaceCache;
        const auto surfaceAt = [&](int x, int z) {
            const auto key = std::make_pair(x, z);
            if (const auto it = surfaceCache.find(key); it != surfaceCache.end())
                return it->second;
            return surfaceCache[key] = probe.surface(x, z);
        };
        while (!queue.empty()) {
            const Pending state = queue.front();
            queue.pop_front();
            const Piece piece = start.pieces[state.piece];
            const bool rigid = !piece.terrainMatching;
            const int pieceMinY = piece.box.y0;
            std::shared_ptr<Space> inside;
            for (const PlacedJigsaw& jig : JigsawsOf(*piece.tmpl, piece.turns, piece.x, piece.y, piece.z, random)) {
                int tx = jig.x, ty = jig.y, tz = jig.z;
                Step(jig.front, tx, ty, tz);
                const int jy = jig.y - pieceMinY;
                const std::string poolName = StripNamespace(jig.j->pool);
                const auto& pool = store->Pool(poolName);
                const std::string fallbackName = store->Fallback(poolName);
                const auto& fallback = store->Pool(fallbackName);
                if (pool.empty() && fallback.empty())
                    continue;
                std::shared_ptr<Space> space;
                if (piece.box.Inside(tx, ty, tz)) {
                    if (!inside) {
                        inside = std::make_shared<Space>();
                        inside->outer = piece.box;
                    }
                    space = inside;
                } else {
                    space = state.space;
                }
                std::vector<std::string> candidates;
                if (state.depth != def.depth) {
                    candidates = pool;
                    Shuffle(candidates, random);
                }
                std::vector<std::string> fb = fallback;
                Shuffle(fb, random);
                candidates.insert(candidates.end(), fb.begin(), fb.end());
                bool placed = false;
                for (const std::string& name : candidates) {
                    const auto child = store->Get(name);
                    if (!child)
                        continue;
                    std::vector<int> rotations = {0, 1, 2, 3};
                    Shuffle(rotations, random);
                    for (const int r : rotations) {
                        const std::vector<PlacedJigsaw> childJigsaws = JigsawsOf(*child, r, 0, 0, 0, random);
                        const Box box0 = BoxOf(*child, r, 0, 0, 0);
                        int expansion = 0;
                        if (def.expansion && box0.y1 - box0.y0 + 1 <= 16)
                            for (const PlacedJigsaw& cj : childJigsaws) {
                                int qx = cj.x, qy = cj.y, qz = cj.z;
                                Step(cj.front, qx, qy, qz);
                                if (box0.Inside(qx, qy, qz))
                                    expansion = std::max(expansion, store->MaxHeight(cj.j->pool));
                            }
                        for (const PlacedJigsaw& cj : childJigsaws) {
                            if (!CanAttach(jig, cj))
                                continue;
                            const int ox = tx - cj.x, oz = tz - cj.z;
                            Box box = BoxOf(*child, r, ox, ty - cj.y, oz);
                            // Village streets and their dead ends follow the ground; everything else keeps its shape.
                            const bool childRigid = !(name.starts_with("village/") && (name.contains("/streets/") || name.contains("/terminators/")));
                            const int cjy = cj.y;
                            const int dy = jy - cjy + (jig.front == "up" ? 1 : jig.front == "down" ? -1 : 0);
                            int newY;
                            if (rigid && childRigid)
                                newY = pieceMinY + dy;
                            else
                                newY = surfaceAt(jig.x, jig.z) - cjy;
                            const int yShift = newY - box.y0;
                            box.y0 += yShift;
                            box.y1 += yShift;
                            const int childY = ty - cj.y + yShift;
                            Box check = box;
                            if (expansion > 0)
                                check.y1 = std::max(check.y1, check.y0 + std::max(expansion + 1, check.y1 - check.y0));
                            if (!space->Fits(check))
                                continue;
                            // The game keeps the raised box as the piece's own, so the houses a
                            // street holds fit inside it.
                            box = check;
                            space->used.push_back(check);
                            Piece next{child, r, ox, childY, oz, box, !childRigid, std::string_view(def.name).starts_with("village"),
                                       childRigid ? piece.ground - dy : 1};
                            start.pieces.push_back(next);
                            if (state.depth + 1 <= def.depth)
                                queue.push_back({start.pieces.size() - 1, space, state.depth + 1});
                            placed = true;
                            break;
                        }
                        if (placed)
                            break;
                    }
                    if (placed)
                        break;
                }
            }
        }
        return true;
    }

    std::shared_ptr<Start> StructureGenerator::Generate(const Def& def, int chunkX, int chunkZ) {
        auto start = std::make_shared<Start>();
        start->type = def.name;
        start->chunkX = chunkX;
        start->chunkZ = chunkZ;
        start->adaptation = def.adaptation;
        if (def.kind == Kind::Code) {
            WorldRandom random = LargeFeature(probe.seed, chunkX, chunkZ);
            if (!def.build || !def.build(random, probe, store.get(), chunkX, chunkZ, *start) || start->pieces.empty())
                return nullptr;
        } else if (!store) {
            return nullptr; // template structures need the pack
        } else if (def.kind == Kind::Jigsaw) {
            if (!Assemble(def, *start, chunkX, chunkZ))
                return nullptr;
        } else if (!def.startPool.empty()) {
            WorldRandom random = LargeFeature(probe.seed, chunkX, chunkZ);
            const int turns = random.NextInt(4);
            std::vector<std::string> names;
            for (const std::string& p : def.startPool) {
                const auto& pool = store->Pool(p);
                if (!pool.empty())
                    names.insert(names.end(), pool.begin(), pool.end());
                else if (store->Get(p))
                    names.push_back(p);
            }
            if (names.empty())
                return nullptr;
            const auto tmpl = store->Get(names[static_cast<size_t>(random.NextInt(static_cast<int>(names.size())))]);
            if (!tmpl)
                return nullptr;
            const int x = chunkX * 16 + random.NextInt(8), z = chunkZ * 16 + random.NextInt(8);
            int y;
            const std::string_view type = def.name;
            if (probe.dimension == kNether)
                y = 32 + random.NextInt(64);
            else
                y = probe.surface(x + tmpl->sx / 2, z + tmpl->sz / 2) - 1;
            if ((type == "ocean_ruin" || type == "shipwreck") && probe.ground)
                y = probe.ground(x, z) - 1; // on the sea floor
            if (!def.biomes.empty()) {
                const int b = probe.biome(x, y, z);
                if (b < 0 || b >= vanilla::kBiomeCount ||
                    std::ranges::find(def.biomes, std::string_view(vanilla::kBiomes[b].java)) == def.biomes.end())
                    return nullptr;
            }
            if (type == "ruined_portal")
                y -= random.NextInt(3); // part buried
            Piece p{tmpl, turns, x, y, z, BoxOf(*tmpl, turns, x, y, z), false, false, 1};
            start->pieces.push_back(p);
            if (def.kind == Kind::Outpost) {
                // The watchtower with a few camp features around it (tents, cages, logs, targets).
                static const char* const kFeatures[] = {"pillageroutpost/feature_cage1", "pillageroutpost/feature_cage2",
                                                        "pillageroutpost/feature_logs", "pillageroutpost/feature_targets",
                                                        "pillageroutpost/feature_tent1", "pillageroutpost/feature_tent2"};
                const int count = 2 + random.NextInt(3);
                for (int i = 0; i < count; i++) {
                    const auto f = store->Get(kFeatures[random.NextInt(6)]);
                    if (!f)
                        continue;
                    const double a = random.NextDouble() * 2 * 3.141592653589793;
                    const int fx = x + tmpl->sx / 2 + static_cast<int>(std::cos(a) * (12 + random.NextInt(8)));
                    const int fz = z + tmpl->sz / 2 + static_cast<int>(std::sin(a) * (12 + random.NextInt(8)));
                    const int ft = random.NextInt(4);
                    const int fy = probe.surface(fx, fz) - 1;
                    Piece q{f, ft, fx, fy, fz, BoxOf(*f, ft, fx, fy, fz), false, false, 1};
                    bool clash = false;
                    for (const Piece& o : start->pieces)
                        clash = clash || o.box.Intersects(q.box);
                    if (!clash)
                        start->pieces.push_back(q);
                }
            }
        } else {
            return nullptr;
        }
        start->box = start->pieces.front().box;
        for (const Piece& p : start->pieces)
            start->box.Encapsulate(p.box);
        return start;
    }

    std::shared_ptr<const Start> StructureGenerator::StartAt(const Set& set, int chunkX, int chunkZ) {
        const auto key = std::make_tuple(&set, chunkX, chunkZ);
        {
            std::lock_guard lock(mutex);
            if (const auto it = starts.find(key); it != starts.end())
                return it->second;
        }
        std::shared_ptr<Start> made;
        if (IsStartChunk(set, probe.seed, chunkX, chunkZ)) {
            bool excluded = false;
            if (set.exclusionChunks > 0)
                if (const Set* villages = SetNamed("villages"))
                    for (int dx = -set.exclusionChunks; dx <= set.exclusionChunks && !excluded; dx++)
                        for (int dz = -set.exclusionChunks; dz <= set.exclusionChunks && !excluded; dz++)
                            excluded = IsStartChunk(*villages, probe.seed, chunkX + dx, chunkZ + dz);
            if (!excluded) {
                if (set.structures.size() == 1) {
                    made = Generate(set.structures[0], chunkX, chunkZ);
                } else {
                    // The game's weighted pick among the set's structures, dropping each that fails.
                    std::vector<const Def*> entries;
                    int total = 0;
                    for (const Def& d : set.structures) {
                        entries.push_back(&d);
                        total += d.weight;
                    }
                    WorldRandom random = LargeFeature(probe.seed, chunkX, chunkZ);
                    while (!entries.empty() && !made) {
                        int r = random.NextInt(total);
                        size_t idx = 0;
                        for (const Def* e : entries) {
                            r -= e->weight;
                            if (r < 0)
                                break;
                            idx++;
                        }
                        idx = std::min(idx, entries.size() - 1);
                        const Def* chosen = entries[idx];
                        made = Generate(*chosen, chunkX, chunkZ);
                        total -= chosen->weight;
                        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(idx));
                    }
                }
            }
        }
        std::lock_guard lock(mutex);
        return starts[key] = made;
    }

    std::vector<std::shared_ptr<const Start>> StructureGenerator::StartsNear(int chunkX, int chunkZ) {
        std::vector<std::shared_ptr<const Start>> out;
        constexpr int kReach = 8;
        const Box chunkBox{chunkX * 16, probe.minY, chunkZ * 16, chunkX * 16 + 15, probe.maxY, chunkZ * 16 + 15};
        for (const Set* set : sets) {
            std::set<std::pair<int, int>> seen;
            if (set->spacing == 0) {
                for (const auto& [sx, sz] : StrongholdChunks(probe.seed))
                    if (std::abs(sx - chunkX) <= kReach && std::abs(sz - chunkZ) <= kReach)
                        if (auto start = StartAt(*set, sx, sz); start && start->box.Intersects(chunkBox))
                            out.push_back(start);
                continue;
            }
            const int r0x = FloorDiv(chunkX - kReach, set->spacing), r1x = FloorDiv(chunkX + kReach, set->spacing);
            const int r0z = FloorDiv(chunkZ - kReach, set->spacing), r1z = FloorDiv(chunkZ + kReach, set->spacing);
            for (int rx = r0x; rx <= r1x; rx++)
                for (int rz = r0z; rz <= r1z; rz++) {
                    const auto [sx, sz] = PotentialStart(*set, probe.seed, rx * set->spacing, rz * set->spacing);
                    if (std::abs(sx - chunkX) > kReach || std::abs(sz - chunkZ) > kReach || !seen.insert({sx, sz}).second)
                        continue;
                    if (auto start = StartAt(*set, sx, sz); start && start->box.Intersects(chunkBox))
                        out.push_back(start);
                }
        }
        return out;
    }

    namespace {
        // A /locate name against a structure: the game's names, and Bedrock's group names
        // ("temple" for the four scattered temples, "ruins" for ocean ruins).
        bool TypeMatches(const std::string& type, std::string_view name) {
            if (type == name)
                return true;
            if (type == "village")
                return name.starts_with("village");
            if (type == "temple")
                return name == "desert_pyramid" || name == "jungle_pyramid" || name == "swamp_hut" || name == "igloo";
            if (type == "ruins" || type == "ocean_ruins")
                return name == "ocean_ruin";
            if (type == "fortress")
                return name == "fortress";
            if (type == "bastion" || type == "bastion_remnant")
                return name == "bastion_remnant";
            if (type == "endcity" || type == "end_city")
                return name == "end_city";
            return false;
        }
    }

    bool StructureGenerator::Locate(const std::string& type, int x, int z, int radius, int& outX, int& outZ) {
        const int cx = x >> 4, cz = z >> 4;
        for (const Set* set : sets) {
            bool has = false;
            for (const Def& d : set->structures)
                has = has || TypeMatches(type, d.name);
            if (!has)
                continue;
            double best = 1e300;
            bool found = false;
            if (set->spacing == 0) {
                for (const auto& [sx, sz] : StrongholdChunks(probe.seed)) {
                    const double d = std::hypot(sx - cx, sz - cz);
                    if (d < best && d <= radius) {
                        best = d;
                        outX = sx * 16 + 8;
                        outZ = sz * 16 + 8;
                        found = true;
                    }
                }
                if (found)
                    return true;
                continue;
            }
            const int r0x = FloorDiv(cx - radius, set->spacing), r1x = FloorDiv(cx + radius, set->spacing);
            const int r0z = FloorDiv(cz - radius, set->spacing), r1z = FloorDiv(cz + radius, set->spacing);
            for (int rx = r0x; rx <= r1x; rx++)
                for (int rz = r0z; rz <= r1z; rz++) {
                    const auto [sx, sz] = PotentialStart(*set, probe.seed, rx * set->spacing, rz * set->spacing);
                    const double d = std::hypot(sx - cx, sz - cz);
                    if (d >= best)
                        continue;
                    const auto start = StartAt(*set, sx, sz);
                    if (!start || !TypeMatches(type, start->type))
                        continue;
                    best = d;
                    outX = (start->box.x0 + start->box.x1) / 2;
                    outZ = (start->box.z0 + start->box.z1) / 2;
                    found = true;
                }
            if (found)
                return true;
        }
        return false;
    }

    void StructureGenerator::Place(int chunkX, int chunkZ, ChunkColumn& out) {
        const auto list = StartsNear(chunkX, chunkZ);
        if (list.empty()) {
            PlaceChunkFeatures(probe.seed, probe, store.get(), chunkX, chunkZ, out);
            return;
        }
        const int baseX = chunkX * 16, baseZ = chunkZ * 16;
        // The surface of this chunk before any piece goes in (terrain-matching pieces follow it).
        std::array<int, 256> surface{};
        for (int lx = 0; lx < 16; lx++)
            for (int lz = 0; lz < 16; lz++) {
                int y = probe.maxY;
                while (y > probe.minY && out.Get(lx, y, lz) == BlockRegistry::kAir)
                    y--;
                surface[static_cast<size_t>(lx * 16 + lz)] = y + 1;
            }
        static const uint16_t dirt = BlockCatalog::Instance().DefaultBlock("minecraft:dirt");
        static const uint16_t grass = BlockCatalog::Instance().DefaultBlock("minecraft:grass_block");
        static const uint16_t chestBlock = BlockCatalog::Instance().DefaultBlock("minecraft:chest");
        (void)chestBlock;
        const Box chunkBox{baseX, probe.minY, baseZ, baseX + 15, probe.maxY, baseZ + 15};
        for (const auto& start : list) {
            for (const Piece& p : start->pieces) {
                if (!p.box.Intersects(chunkBox))
                    continue;
                const Template& t = *p.tmpl;
                const std::vector<uint16_t>& palette = t.Bedrock(p.turns);
                // Beard: rigid pieces of structures that adapt the terrain get the ground cut away above
                // their floor and built up under it.
                if (start->adaptation == 1 && !p.terrainMatching) {
                    for (int wx = std::max(p.box.x0, baseX); wx <= std::min(p.box.x1, baseX + 15); wx++)
                        for (int wz = std::max(p.box.z0, baseZ); wz <= std::min(p.box.z1, baseZ + 15); wz++) {
                            const int lx = wx - baseX, lz = wz - baseZ;
                            const int floorY = p.box.y0 + p.ground - 1;
                            const uint16_t top = surface[static_cast<size_t>(lx * 16 + lz)] - 1 >= probe.minY
                                                     ? out.Get(lx, surface[static_cast<size_t>(lx * 16 + lz)] - 1, lz)
                                                     : dirt;
                            for (int y = floorY + 1; y <= p.box.y1 && y <= probe.maxY; y++)
                                if (out.Get(lx, y, lz) != BlockRegistry::kAir && !BlockRegistry::Instance().Name(out.Get(lx, y, lz)).contains("water"))
                                    out.Set(lx, y, lz, BlockRegistry::kAir);
                            for (int y = floorY, depth = 0; y > probe.minY && depth < 12; y--, depth++) {
                                const uint16_t here = out.Get(lx, y, lz);
                                if (here != BlockRegistry::kAir && !BlockRegistry::Instance().Name(here).contains("water"))
                                    break;
                                out.Set(lx, y, lz, depth == 0 ? (top == grass || top == dirt ? grass : top) : (top == grass ? dirt : top));
                            }
                        }
                }
                // Pieces the game props up (the desert pyramid): every column of the footprint is
                // filled down from the given height to the first solid ground.
                if (!p.fillDown.empty()) {
                    const uint16_t fill = BlockCatalog::Instance().DefaultBlock(p.fillDown);
                    for (int wx = std::max(p.box.x0, baseX); wx <= std::min(p.box.x1, baseX + 15); wx++)
                        for (int wz = std::max(p.box.z0, baseZ); wz <= std::min(p.box.z1, baseZ + 15); wz++)
                            for (int y = p.fillTop; y > probe.minY; y--) {
                                const uint16_t here = out.Get(wx - baseX, y, wz - baseZ);
                                if (here != BlockRegistry::kAir) {
                                    const std::string n = BlockRegistry::Instance().Name(here);
                                    if (!n.contains("water") && !n.contains("lava") && !n.contains("short_grass") &&
                                        !n.contains("tall_grass") && !n.contains("deadbush") && !n.contains("cactus") &&
                                        !n.contains("snow_layer") && !n.contains("fern") && !n.contains("bush"))
                                        break;
                                }
                                out.Set(wx - baseX, y, wz - baseZ, fill);
                            }
                }
                for (const TemplateBlock& b : t.blocks) {
                    if (t.isStructureBlock[b.state])
                        continue;
                    if (p.legacy && t.isAir[b.state])
                        continue;
                    int rx, rz;
                    Turn(p.turns, b.x, b.z, rx, rz);
                    const int wx = p.x + rx, wz = p.z + rz;
                    if (wx < baseX || wx > baseX + 15 || wz < baseZ || wz > baseZ + 15)
                        continue;
                    const int lx = wx - baseX, lz = wz - baseZ;
                    int wy = p.y + b.y;
                    if (p.terrainMatching)
                        wy = surface[static_cast<size_t>(lx * 16 + lz)] - 1 + b.y;
                    if (wy < probe.minY || wy > probe.maxY)
                        continue;
                    uint16_t block = palette[b.state];
                    if (t.isJigsaw[b.state]) {
                        std::string final = "minecraft:air";
                        if (b.nbt >= 0)
                            final = t.nbts[static_cast<size_t>(b.nbt)].String("final_state", "minecraft:air");
                        if (final.contains("structure_void"))
                            continue;
                        block = ToBedrock(Rotated(ParseState(final), p.turns));
                    } else if (block == BlockRegistry::kAir && !t.isAir[b.state]) {
                        continue; // structure void and states with no Bedrock block
                    }
                    out.Set(lx, wy, lz, block);
                    if (b.nbt >= 0) {
                        const nbt::Value& tag = t.nbts[static_cast<size_t>(b.nbt)];
                        const std::string loot = tag.String("LootTable");
                        if (!loot.empty())
                            out.generated.push_back({GeneratedExtra::Loot, wx, wy, wz, StripNamespace(loot)});
                    }
                }
                // Data markers that spawn mobs ("minecraft:villager_v2:minecraft:spawn_from_village").
                for (const DataMarker& m : t.markers) {
                    // End city markers: shulker sentries and the treasure chests.
                    std::string text = m.text;
                    if (text == "Sentry")
                        text = "minecraft:shulker";
                    if (text.starts_with("Chest") && start->type == "end_city") {
                        int rx0, rz0;
                        Turn(p.turns, m.x, m.z, rx0, rz0);
                        const int wx0 = p.x + rx0, wz0 = p.z + rz0, wy0 = p.y + m.y - 1;
                        if (wx0 >= baseX && wx0 <= baseX + 15 && wz0 >= baseZ && wz0 <= baseZ + 15 && wy0 >= probe.minY) {
                            out.Set(wx0 - baseX, wy0, wz0 - baseZ, BlockCatalog::Instance().DefaultBlock("minecraft:chest"));
                            out.generated.push_back({GeneratedExtra::Loot, wx0, wy0, wz0, "chests/end_city_treasure"});
                        }
                        continue;
                    }
                    if (!text.starts_with("minecraft:"))
                        continue;
                    int rx, rz;
                    Turn(p.turns, m.x, m.z, rx, rz);
                    const int wx = p.x + rx, wz = p.z + rz;
                    if (wx < baseX || wx > baseX + 15 || wz < baseZ || wz > baseZ + 15)
                        continue;
                    const size_t second = text.find(':', 10);
                    const std::string id = text.substr(0, second);
                    int wy = p.y + m.y;
                    if (p.terrainMatching)
                        wy = surface[static_cast<size_t>((wx - baseX) * 16 + (wz - baseZ))] - 1 + m.y;
                    out.Set(wx - baseX, wy, wz - baseZ, BlockRegistry::kAir);
                    out.generated.push_back({GeneratedExtra::Entity, wx, wy, wz, id});
                }
                for (const TemplateEntity& e : t.entities) {
                    int rx, rz;
                    Turn(p.turns, static_cast<int>(std::floor(e.x)), static_cast<int>(std::floor(e.z)), rx, rz);
                    const int wx = p.x + rx, wz = p.z + rz;
                    if (wx < baseX || wx > baseX + 15 || wz < baseZ || wz > baseZ + 15 || e.id.empty())
                        continue;
                    int wy = p.y + static_cast<int>(std::floor(e.y));
                    if (p.terrainMatching)
                        wy = surface[static_cast<size_t>((wx - baseX) * 16 + (wz - baseZ))] - 1 + static_cast<int>(std::floor(e.y));
                    out.generated.push_back({GeneratedExtra::Entity, wx, wy, wz, e.id});
                }
            }
        }
        PlaceChunkFeatures(probe.seed, probe, store.get(), chunkX, chunkZ, out);
    }
}
