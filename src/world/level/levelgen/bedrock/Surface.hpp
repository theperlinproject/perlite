#pragma once
// The surface pass: bedrock floor, then each column's top, filler and sea-floor materials from its biome's
// surface builder (overworld, swamp, frozen ocean, mesa, noise gradient), with the material adjustments,
// steep-slope material, icebergs and badlands bands.

#include "world/level/levelgen/bedrock/BiomeVolume.hpp"
#include "world/level/levelgen/bedrock/Fill.hpp"
#include "world/level/levelgen/bedrock/Pack.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace perlite::bedrock {

    // 2D/3D simplex noise as the surface code samples it, and its octave stack.
    class SurfaceSimplex {
    public:
        template <class R>
        SurfaceSimplex(R& random, bool newOrder);
        float Corners2d(float x, float y) const;
        float Sample2d(float x, float y) const { return Corners2d(x, y) * 70.0f; }
        float Sample3d(float x, float y, float z) const;
        void AddRegion(std::array<float, 256>& buffer, float originX, float originZ, float step, float amplitude) const;

    private:
        int P(int i) const { return perm[static_cast<size_t>(i)]; }
        std::array<float, 3> origin;
        std::array<uint8_t, 512> perm;
    };

    class PerlinSimplex {
    public:
        template <class R>
        PerlinSimplex(R& random, int levels, bool newOrder) {
            for (int i = 0; i < levels; i++)
                this->levels.emplace_back(random, newOrder);
        }
        float Sum2d(float x, float z) const;
        float Sum3d(float x, float y, float z) const;
        std::array<float, 256> Region(int chunkX, int chunkZ) const;

    private:
        std::vector<SurfaceSimplex> levels;
    };

    struct SurfacePalette {
        uint16_t air = 0, bedrock, stone, water, lava, sand, redSand, suspiciousSand, sandstone, redSandstone, gravel, ice, packedIce,
            blueIce, snowBlock, coarseDirt, grassBlock, netherrack, hardenedClay;
        std::array<uint16_t, 16> terracotta;
        std::array<uint16_t, 7> foundation;
        SurfacePalette();
        bool IsFoundation(uint16_t b) const { return std::find(foundation.begin(), foundation.end(), b) != foundation.end(); }
        bool IsLiquid(uint16_t b) const { return b == water || b == lava; }
        bool IsSand(uint16_t b) const { return b == sand || b == redSand || b == suspiciousSand; }
    };

    struct BiomeSurface;

    class SurfacePass {
    public:
        SurfacePass(int64_t seed, const PositionalFactory& factory, const Pack& pack);
        ~SurfacePass();
        void Build(const BedrockTerrain& terrain, ProtoChunk& chunk, const BiomeVolume& biomes) const;
        // A biome's base temperature and whether it is partially frozen, from its document.
        std::optional<std::pair<float, bool>> Climate(int biomeId) const;
        // Temperature at a block, cooled with height, as snow and ice placement read it.
        float TemperatureAt(int biomeId, int x, int y, int z, int seaLevel) const;

    private:
        struct Temperature;
        std::array<int, 256> BuildColumns(const BedrockTerrain& terrain, ProtoChunk& chunk, const BiomeVolume& biomes) const;
        void BuildGradients(ProtoChunk& chunk, const BiomeVolume& biomes, const std::array<int, 256>& lowerBounds) const;
        bool IsSteep(const ProtoChunk& chunk, const std::array<int, 256>& tops, int lx, int lz, const struct Steep& steep) const;

        SurfacePalette palette;
        std::unordered_map<int, std::unique_ptr<BiomeSurface>> biomes;  // by biome id
        PerlinSimplex depth, adjustment, swamp;
        std::unique_ptr<Temperature> temperature;
        NormalNoise icebergSurface, icebergPillar, icebergRoof;
        PositionalFactory icebergFactory;
        NormalNoise clayBandsOffset, badlandsSurface, badlandsPillar, badlandsRoof;
        std::array<uint8_t, 192> bands;
    };
}
