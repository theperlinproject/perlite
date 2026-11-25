#pragma once
// The Bedrock overworld as a terrain backend: noise fill with aquifers and ore veins, block-resolution
// biomes, the surface pass and the legacy carvers.

#include "server/Config.hpp"
#include "world/level/levelgen/Generator.hpp"
#include "world/level/levelgen/bedrock/BiomeVolume.hpp"
#include "world/level/levelgen/bedrock/Biomes.hpp"
#include "world/level/levelgen/bedrock/Carvers.hpp"
#include "world/level/levelgen/bedrock/Fill.hpp"
#include "world/level/levelgen/bedrock/Pack.hpp"
#include "world/level/levelgen/bedrock/Surface.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace perlite::bedrock {

    class OverworldBackend final : public TerrainBackend {
    public:
        static std::unique_ptr<OverworldBackend> Create(const ServerConfig& config, std::string& error);
        ~OverworldBackend() override;

        const char* Name() const override { return "bedrock/overworld"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 1; }
        bool ThreadSafe() const override { return true; }
        bool HasBiomes() const override { return true; }
        bool FindSpawn(int& x, int& y, int& z) override;

        // One chunk through fill, surface and carvers (tests and tools).
        ProtoChunk Build(int chunkX, int chunkZ, BiomeVolume* biomesOut = nullptr) const;
        const BedrockTerrain& Terrain() const { return *terrain; }
        int BiomeAt(int x, int y, int z) const;

    private:
        OverworldBackend() = default;
        int PreliminarySurface(int quartX, int quartZ) const;
        std::vector<int> BiomeSamples(int chunkX, int chunkZ) const;
        std::shared_ptr<const CornerColumns> Corners(int chunkX, int chunkZ) const;

        int64_t seed = 0;
        std::shared_ptr<const Pack> pack;
        std::unique_ptr<BedrockTerrain> terrain;
        std::unique_ptr<BufferNoises> buffers;
        std::unique_ptr<TerrainFill> fill;
        std::unique_ptr<SurfacePass> surface;
        std::unique_ptr<Carvers> carvers;
        struct Caches;
        std::unique_ptr<Caches> caches;
    };

    // The pack folder the generator reads: $PERLITE_VANILLA_PACK, else the installed vanilla behavior pack.
    std::string PackFolder(const ServerConfig& config);
}
