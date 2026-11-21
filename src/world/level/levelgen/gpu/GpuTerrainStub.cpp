#include "world/level/levelgen/gpu/GpuTerrain.hpp"

namespace perlite {
    std::unique_ptr<TerrainBackend> CreateGpuTerrainBackend(const ServerConfig&, std::string& note) {
        note = "built without gpu terrain (PERLITE_GPU=OFF)";
        return nullptr;
    }

    std::unique_ptr<TerrainBackend> CreateGpuVanillaBackend(const ServerConfig&, std::string& note) {
        note = "built without gpu terrain (PERLITE_GPU=OFF)";
        return nullptr;
    }

    std::unique_ptr<TerrainBackend> CreateGpuDimensionBackend(const ServerConfig&, int, std::string& note) {
        note = "built without gpu terrain (PERLITE_GPU=OFF)";
        return nullptr;
    }
}
