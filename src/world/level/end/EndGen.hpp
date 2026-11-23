#pragma once
#include "world/level/chunk/Chunk.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace perlite::theend {
    // One of the ten obsidian pillars around the main island, as the game lays them out from the
    // world seed: ten slots on a circle of radius 42, sizes shuffled over them.
    struct Spike {
        int x = 0, z = 0;
        int radius = 0;
        int height = 0;
        bool guarded = false; // the two smallest have an iron bar cage around the crystal
    };
    const std::vector<Spike>& Spikes(int64_t seed);

    // The positions of the twenty end gateways that open, one per dragon killed (in the order they
    // open), on a circle of radius 96 at y 75.
    struct GatewaySlot {
        int x = 0, y = 75, z = 0;
    };
    const std::vector<GatewaySlot>& Gateways(int64_t seed);

    // The End's features in one chunk: the obsidian pillars (and the bedrock under each crystal),
    // chorus plants on the highlands and the small floating islands. `region` holds the undecorated
    // chunks around (kDecorationSide x kDecorationSide, see Generator.hpp); only `out` is written.
    struct Biomes {
        int theEnd = -1, highlands = -1, smallIslands = -1;
    };
    void Decorate(int64_t seed, int chunkX, int chunkZ, const ChunkColumn* const region[], ChunkColumn& out,
                  const Biomes& biomes);

    // Every block of one pillar, in world coordinates (the respawn rebuilds them one by one).
    void PlaceSpike(const Spike& spike, const std::function<void(int x, int y, int z, uint16_t block)>& set);

    // Writes the spike blocks that fall in the chunk (used when the fight is reset to rebuild them).
    void PlaceSpikes(int64_t seed, int chunkX, int chunkZ, ChunkColumn& out);
}
