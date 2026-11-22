#pragma once
#include <cstdint>

namespace perlite {
    class ChunkColumn;

    // Bedrock keeps the water around a waterlogged block (kelp, seagrass, a slab or fence in water)
    // in a second block layer. These tell which blocks take part.
    namespace waterlog {
        bool IsWater(uint16_t id);
        // Blocks that can hold water in the second layer.
        bool Holds(uint16_t id);
        // Blocks that only exist under water (kelp, seagrass), always with water in the second layer.
        bool NeedsWater(uint16_t id);
        uint16_t Water();
        // Gives water to the generated blocks that sit in water: always for kelp and seagrass, and for
        // the other blocks that hold water when water touches them.
        void FillGenerated(ChunkColumn& column);
    }
}
