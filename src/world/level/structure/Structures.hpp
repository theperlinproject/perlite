#pragma once
#include "world/level/chunk/Chunk.hpp"
#include "world/level/structure/Templates.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace perlite::structure {
    struct Box {
        int x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0; // inclusive
        bool Intersects(const Box& o) const {
            return x0 <= o.x1 && x1 >= o.x0 && y0 <= o.y1 && y1 >= o.y0 && z0 <= o.z1 && z1 >= o.z0;
        }
        bool Contains(const Box& o) const {
            return o.x0 >= x0 && o.x1 <= x1 && o.y0 >= y0 && o.y1 <= y1 && o.z0 >= z0 && o.z1 <= z1;
        }
        bool Inside(int x, int y, int z) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1; }
        void Encapsulate(const Box& o);
    };

    // One placed template of a structure.
    struct Piece {
        std::shared_ptr<const Template> tmpl;
        int turns = 0;          // clockwise quarter turns about the template's origin
        int x = 0, y = 0, z = 0; // where the template's origin goes
        Box box;
        bool terrainMatching = false; // follows the ground (village streets) instead of keeping its shape
        bool legacy = false;          // its air does not replace what is there
        int ground = 1;               // the game's ground level delta
    };

    // A structure as generated at its start chunk.
    struct Start {
        std::string type;
        int chunkX = 0, chunkZ = 0;
        std::vector<Piece> pieces;
        Box box;
        int adaptation = 0; // 0 none, 1 beard (terrain raised or cut to meet the pieces), 2 bury
    };

    // Where the generator samples the world without generating chunks.
    struct WorldProbe {
        int64_t seed = 0;
        int dimension = 0;
        int minY = -64, maxY = 319;
        // The first free block above the noise terrain (water counts as filled), like the game's
        // WORLD_SURFACE_WG height from the noise alone.
        std::function<int(int x, int z)> surface;
        // The first free block above the noise terrain's solid ground (water does not count).
        std::function<int(int x, int z)> ground;
        // The biome (index into the biome table) at a block.
        std::function<int(int x, int y, int z)> biome;
    };

    // Structures of one dimension: where they start (the game's placement grids and salts), what
    // they are made of (templates of the Bedrock vanilla pack joined by their jigsaws), and writing
    // the pieces that fall in a chunk while it is generated. Thread-safe.
    class StructureGenerator {
    public:
        StructureGenerator(std::shared_ptr<TemplateStore> store, WorldProbe probe);

        // Writes every structure piece reaching into the chunk; called before the chunk's features.
        void Place(int chunkX, int chunkZ, ChunkColumn& out);
        // The start of a structure type nearest (x, z) within `radius` chunks, by its start chunk.
        bool Locate(const std::string& type, int x, int z, int radius, int& outX, int& outZ);
        // The starts whose pieces reach into a chunk (for tests and /locate).
        std::vector<std::shared_ptr<const Start>> StartsNear(int chunkX, int chunkZ);
        const TemplateStore& Store() const { return *store; }

        struct Def;
        struct Set;

    private:
        std::shared_ptr<const Start> StartAt(const Set& set, int chunkX, int chunkZ);
        std::shared_ptr<Start> Generate(const Def& def, int chunkX, int chunkZ);
        bool Assemble(const Def& def, Start& start, int chunkX, int chunkZ);

        std::shared_ptr<TemplateStore> store;
        WorldProbe probe;
        std::vector<const Set*> sets;
        std::mutex mutex;
        std::map<std::tuple<const Set*, int, int>, std::shared_ptr<const Start>> starts;
    };
}
