// Two worlds generated one after the other in one process must not share generator state: the
// second world's chunks are the same as when it is generated alone.
#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"

#include <cstdio>
#include <vector>

using namespace perlite;

namespace {
    std::vector<uint16_t> Chunk(int64_t seed, int cx, int cz, bool spawn) {
        ServerConfig config;
        config.seed = seed;
        config.randomSeed = false;
        std::string error;
        auto backend = vanilla::VanillaCpuBackend::Create(config, error);
        if (!backend) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return {};
        }
        if (spawn) {
            int x, y, z;
            backend->FindSpawn(x, y, z);
        }
        std::vector<ChunkPos> pos;
        for (int dx = -1; dx <= 1; dx++)
            for (int dz = -1; dz <= 1; dz++)
                pos.push_back({cx + dx, cz + dz});
        std::vector<ChunkColumn> cols(pos.size());
        backend->Generate(pos, cols);
        std::vector<uint16_t> out;
        const ChunkColumn& c = cols[4];
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 384; y++)
                    out.push_back(c.Get(x, y, z));
        return out;
    }
}

int main() {
    BlockCatalog::Instance().LoadBuiltin();
    const int64_t seed = 6618995517318049792LL;
    const std::vector<uint16_t> alone = Chunk(seed, 3, -3, true);
    (void)Chunk(12345, 3, -3, true);
    (void)Chunk(-987654321, 15, -3, true);
    const std::vector<uint16_t> after = Chunk(seed, 3, -3, true);
    if (alone.empty() || alone != after) {
        size_t diff = 0;
        for (size_t i = 0; i < alone.size() && i < after.size(); i++)
            diff += alone[i] != after[i];
        std::printf("FAIL: the world differs after other worlds were generated (%zu blocks)\n", diff);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
