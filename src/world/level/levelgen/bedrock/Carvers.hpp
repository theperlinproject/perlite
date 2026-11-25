#pragma once
// Legacy cave and canyon carvers: each chunk within 8 of the one carved may start tunnels, rooms and a canyon
// from its large-feature seed; their ellipsoid steps are cut into the chunk, filled from the aquifer below its
// level.

#include "world/level/levelgen/bedrock/Fill.hpp"

#include <array>
#include <memory>
#include <vector>

namespace perlite::bedrock {

    class Carvers {
    public:
        explicit Carvers(uint32_t levelSeed);
        void Carve(const AquiferNoises& noises, const BedrockTerrain& terrain, const SurfaceAt& surface, ProtoChunk& chunk) const;

        struct Segment {
            float center[3];
            float horizontalRadius, verticalRadius, floorLevel;
            int step, length;
            float thickness;
        };
        struct Step {
            float center[3];
            float horizontalRadius, verticalRadius;
            int remaining;
        };
        struct Canyon {
            float thickness;
            std::array<float, 320> widths;
            std::vector<Step> steps;
        };
        struct Plan {
            std::vector<Segment> caves[2];
            std::unique_ptr<Canyon> canyon;
        };

    private:
        uint8_t Flags(uint16_t block) const;
        Plan PlanAt(int x, int z, int minY) const;
        uint32_t levelSeed;
        std::pair<uint32_t, uint32_t> multipliers;
        mutable std::vector<uint8_t> flags;  // by block id, bit 7 = known
        uint16_t air, sandstone, grass;
    };
}
