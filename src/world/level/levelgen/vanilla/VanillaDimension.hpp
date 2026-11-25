#pragma once
#include "world/level/levelgen/Generator.hpp"
#include "world/level/levelgen/vanilla/DensityFunction.hpp"
#include "world/level/levelgen/vanilla/VanillaAquifer.hpp"
#include "world/level/levelgen/vanilla/VanillaCarvers.hpp"
#include "world/level/levelgen/vanilla/VanillaMaterials.hpp"
#include "world/level/structure/Structures.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace perlite::vanilla {

    class VanillaDimensionBackend final : public TerrainBackend {
    public:
        static std::unique_ptr<VanillaDimensionBackend> Create(const ServerConfig& config, int dimension, std::string& error);

        const char* Name() const override { return dimension == kNether ? "vanilla/nether" : "vanilla/end"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 1; }
        bool ThreadSafe() const override { return true; }
        bool LocateStructure(const std::string& type, int fromX, int fromZ, int radius, int& x, int& z,
                             bool& available) override {
            available = structures != nullptr;
            return structures && structures->Locate(type, fromX, fromZ, radius, x, z);
        }
        bool HasBiomes() const override { return true; }
        // The End decorates its chunks (pillars, chorus, islands); the nether its features.
        bool Decorates() const override { return true; }
        void Decorate(ChunkPos pos, const ChunkColumn* const region[], ChunkColumn& out) const override;
        bool LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel, int& x, int& y,
                         int& z) override;

        int BiomeAt(int x, int y, int z) const;
        float Density(int x, int y, int z) const { return finalDensity->Eval(x, y, z); }
        // The first air above the terrain at a column, from the density alone (0 over the void).
        int SurfaceY(int x, int z) const {
            for (int y = genMinY + genHeight - 1; y > genMinY; y--)
                if (Density(x, y, z) > 0.0f)
                    return y + 1;
            return 0;
        }
        int DimensionId() const { return dimension; }
        const DfPtr& FinalDensity() const { return finalDensity; }
        std::array<DfPtr, 6> ClimateRoots() const { return {temperature, vegetation, zero, erosion, zero, zero}; }
        int GenMinY() const { return genMinY; }
        int GenHeight() const { return genHeight; }
        void Build(int chunkX, int chunkZ, const float* density, ChunkColumn& column) const;
        void PrimeChunkFill(int chunkX, int chunkZ, const int32_t* targets) const;

    private:
        VanillaDimensionBackend() = default;
        const uint8_t* ChunkFill(int chunkX, int chunkZ, const int32_t* primed = nullptr) const;
        int NoiseBiome(int quartX, int quartY, int quartZ) const;
        int SearchNether(const int32_t target[2], int& hint) const;
        int EndBiome(int quartX, int quartY, int quartZ) const;

        int dimension = kNether;
        int64_t worldSeed = 0;
        std::shared_ptr<structure::StructureGenerator> structures;
    public:
        structure::StructureGenerator* Structures() const { return structures.get(); }
    private:
        std::unique_ptr<DataPack> pack;
        std::unique_ptr<DensityGraph> graph;
        DfPtr finalDensity, temperature, vegetation, erosion, zero;
        AquiferFunctions aquifers;
        std::unique_ptr<VanillaMaterials> materials;
        std::unique_ptr<VanillaCarvers> carvers;
        int64_t zoomSeed = 0;
        uint64_t instance = 0;
        int seaLevel = 32;
        int genMinY = 0, genHeight = 128;
        uint16_t defaultBlock = 0, lava = 0;
        struct NetherPoint {
            int32_t temperature, humidity, offset;
            int biome;
        };
        std::vector<NetherPoint> netherPoints;
        int endCenter = 0, endHighlands = 0, endMidlands = 0, endIslands = 0, endBarrens = 0;
    };
}
