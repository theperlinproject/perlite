#pragma once
#include "world/level/chunk/Chunk.hpp"

#include <cstdint>

namespace perlite::vanilla {
    // The nether's features in one chunk: fire, glowstone, the ores (quartz, gold, magma, ancient
    // debris, gravel, blackstone, soul sand), lava springs, mushrooms, the fungus forests with their
    // roots and vines, and basalt. `region` holds the undecorated chunks around (see Generator.hpp).
    void DecorateNether(int64_t seed, int chunkX, int chunkZ, const ChunkColumn* const region[], ChunkColumn& out);
}
