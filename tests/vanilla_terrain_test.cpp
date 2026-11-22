#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace perlite;

int main() {
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = 12345;
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::vector<ChunkPos> pos = {{0, 0}, {1, 0}, {-3, 5}};
    std::vector<ChunkColumn> cols(pos.size());
    const auto start = std::chrono::steady_clock::now();
    backend->Generate(pos, cols);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("%zu chunks in %.0f ms (%.0f ms each)\n", pos.size(), ms,
                ms / static_cast<double>(pos.size()));
    int bad = 0;
    for (size_t n = 0; n < pos.size(); n++) {

        for (int i = 0; i < 400; i++) {
            const int x = (i * 7) & 15, z = (i * 13) & 15, y = -60 + (i * 31) % 360;
            const bool solid = backend->Density(pos[n].x * 16 + x, y, pos[n].z * 16 + z) > 0.0f;
            const uint16_t b = cols[n].Get(x, y, z);
            if (solid && b == 0)
                bad++;
        }
        int height = -999;
        for (int y = 319; y >= -64; y--)
            if (cols[n].Get(8, y, 8) != 0 && cols[n].Get(8, y, 8) != cols[n].Get(0, 319, 0)) {
                height = y;
                break;
            }
        std::printf("chunk (%d,%d): top block at (8,8) y=%d\n", pos[n].x, pos[n].z, height);
    }
    // The overworld bedrock floor, as the game makes it: solid at the two lowest layers, then each column
    // 2 to 5 blocks thick from the bottom, with no gaps.
    {
        const uint16_t bedrock = BlockCatalog::Instance().DefaultBlock("minecraft:bedrock");
        int seen[8] = {};
        bool holes = false;
        for (const ChunkColumn& col : cols)
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    int t = 0;
                    while (t < 8 && col.Get(x, -64 + t, z) == bedrock)
                        t++;
                    seen[t]++;
                    for (int y = -64 + t; y < -56; y++)
                        holes |= col.Get(x, y, z) == bedrock;
                }
        if (seen[0] || seen[1] || seen[6] || seen[7] || !seen[2] || !seen[3] || !seen[4] || !seen[5] || holes) {
            std::printf("bedrock floor: thickness counts 0..7 = %d %d %d %d %d %d %d %d, gaps %d\n", seen[0], seen[1],
                        seen[2], seen[3], seen[4], seen[5], seen[6], seen[7], holes ? 1 : 0);
            return 1;
        }
    }
    if (bad > 40) {
        std::printf("%d solid positions came out as air\n", bad);
        return 1;
    }
    std::printf("vanilla_terrain_test ok\n");
    return 0;
}
