#pragma once
// The Bedrock overworld noise router: climate noises, the terrain splines, the blended base noise and the
// cave noises, evaluated on the 4x8x4 cell grid in single precision.

#include "world/level/levelgen/bedrock/OverworldNoise.hpp"
#include "world/level/levelgen/bedrock/Spline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace perlite::bedrock {

    inline constexpr int kSeaLevel = 63;

    struct DimensionDefinition {
        int minY = -64;
        int height = 384;
        int Top() const { return minY + height; }
    };

    struct ClimateScale {
        float temperature = 1, humidity = 1, continentalness = 1, erosion = 1, weirdness = 1;
    };

    struct TerrainShape {
        LeafTransform offset, factor, jaggedness;
        float globalOffset = -0.50375f;
        float factorScaler = 128.0f;
        int seaLevel = kSeaLevel;
        bool aquifers = true;
        bool oreVeins = true;
    };

    enum class Preset { Default, LargeBiomes, Amplified, AmplifiedLargeBiomes };

    struct OverworldSettings {
        ClimateScale climate;
        TerrainShape terrain;
        bool structures = true;
        static OverworldSettings For(Preset preset);
    };

    using ClimatePoint = std::array<int64_t, 6>;

    struct ClimateColumn {
        float temperature, humidity, continents, erosion, weirdness, offset;
        ClimatePoint Point(int y) const;
    };

    class CaveNoises {
    public:
        explicit CaveNoises(const PositionalFactory& factory);
        float Density(float x, float y, float z, float density) const;

    private:
        float Roughness(float x, float y, float z) const;
        float Spaghetti3d(float x, float y, float z) const;
        float Entrance(float x, float y, float z) const;
        float Cheese(float x, float y, float z, float density) const;
        float Spaghetti2d(float x, float y, float z) const;
        float Pillar(float x, float y, float z) const;
        NormalNoise pillar, pillarRareness, pillarThickness, spaghetti2d, spaghetti2dElevation, spaghetti2dModulator,
            spaghetti2dThickness, spaghetti3d1, spaghetti3d2, spaghetti3dRarity, spaghetti3dThickness, spaghettiRoughness,
            spaghettiRoughnessModulator, caveEntrance, caveLayer, caveCheese;
    };

    inline constexpr int kSurfaceNotFound = 0x7fff;
    inline constexpr int kCellHeight = 8;
    inline constexpr int kCellWidth = 4;

    class BedrockTerrain {
    public:
        BedrockTerrain(const PositionalFactory& factory, const OverworldSettings& settings, DimensionDefinition definition);

        DimensionDefinition Definition() const { return definition; }
        int MinY() const { return definition.minY; }
        int CellsY() const { return definition.height / kCellHeight; }
        int NoiseLevels() const { return CellsY() - 8 + 1; }
        int CornerLevels() const { return CellsY() + 1; }
        int SeaLevel() const { return seaLevel; }
        bool AquifersEnabled() const { return aquifers; }
        bool OreVeinsEnabled() const { return oreVeins; }
        float GlobalOffset() const { return globalOffset; }

        std::pair<float, float> ErosionAndOffset(int quartX, int quartZ) const;
        ClimateColumn Column(int x, int z) const;
        int PreliminarySurface(int quartX, int quartZ) const;
        // Densities at the 4x4 columns of cell corners of the chunk, noise levels per column, bottom up.
        void DensityCorners(int chunkX, int chunkZ, TerrainBuffers& buffers, std::vector<float>& out) const;

    private:
        struct Shape {
            float offset, factor, jaggedness;
        };
        std::pair<float, float> Shifted(float quartX, float quartZ) const;
        SplineInput Input(float x, float z) const;
        float OffsetOf(const SplineInput& in) const { return offsetSpline.Sample(in) + globalOffset; }
        float FactorOf(const SplineInput& in) const { return factorSpline.Sample(in) * factorScaler; }
        Shape ColumnShape(int quartX, int quartZ) const;

        NormalNoise temperature, vegetation, continentalness, erosion, ridge, offset, jagged;
        CaveNoises caves;
        LegacyBlendedNoise blended;
        Spline offsetSpline, factorSpline, jaggednessSpline;
        ClimateScale frequency;
        float globalOffset, factorScaler;
        int seaLevel;
        bool aquifers, oreVeins;
        DimensionDefinition definition;
    };
}
