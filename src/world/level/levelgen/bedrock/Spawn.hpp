#pragma once
#include "world/level/levelgen/bedrock/Terrain.hpp"

#include <utility>

namespace perlite::bedrock {
    // The world spawn column: the inland, non-ridge climate nearest the origin, searched on a coarse ring of
    // 512-block steps out to 2048 and then a fine ring of 32-block steps around the best coarse point.
    std::pair<int, int> SpawnColumn(const BedrockTerrain& terrain);
}
