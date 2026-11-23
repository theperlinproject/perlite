#pragma once
#include "world/level/levelgen/vanilla/JavaRandom.hpp"
#include "world/level/structure/Structures.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace perlite::structure {
    // Writes a template block by block, for the structures the game builds in code (temples,
    // strongholds, mineshafts, fortresses, monuments) and the small features (dungeons, wells,
    // geodes). States are written in the templates' naming and translated like theirs.
    class Builder {
    public:
        Builder(std::string name, int sx, int sy, int sz);
        void Set(int x, int y, int z, std::string_view state);
        void Fill(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view state);
        // Walls, floor and ceiling of the box in `wall`, its inside in `inside` (empty keeps it).
        void Hollow(int x0, int y0, int z0, int x1, int y1, int z1, std::string_view wall, std::string_view inside = "minecraft:air");
        void Chest(int x, int y, int z, std::string_view facing, std::string_view lootTable);
        void Spawner(int x, int y, int z, std::string_view mob);
        void Mob(int x, int y, int z, std::string_view id);
        bool InBounds(int x, int y, int z) const;
        std::shared_ptr<const Template> Done();
        int SizeX() const { return t->sx; }
        int SizeY() const { return t->sy; }
        int SizeZ() const { return t->sz; }

    private:
        uint16_t Index(std::string_view state);
        std::shared_ptr<Template> t;
        std::map<std::string, uint16_t> palette;
        std::map<std::tuple<int, int, int>, size_t> at;
    };

    using vanilla::WorldRandom;

    // Each fills `start` with its pieces; false when nothing generates here.
    bool BuildDesertPyramid(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildJunglePyramid(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildSwampHut(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildBuriedTreasure(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildMineshaft(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildStronghold(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildFortress(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildMonument(WorldRandom& r, const WorldProbe& probe, int chunkX, int chunkZ, Start& start);
    bool BuildEndCity(WorldRandom& r, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, Start& start);
    bool BuildMansion(WorldRandom& r, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, Start& start);

    // Features placed inside one chunk as it decorates: dungeons, desert wells, fossils, geodes.
    void PlaceChunkFeatures(int64_t seed, const WorldProbe& probe, const TemplateStore* store, int chunkX, int chunkZ, ChunkColumn& out);

    // The game's stronghold rings: 128 start chunks on rings around the origin.
    const std::vector<std::pair<int, int>>& StrongholdChunks(int64_t seed);
}
