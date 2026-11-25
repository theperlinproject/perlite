#pragma once
// Block-resolution biomes of a chunk: climate samples every 8 blocks, and in mixed cells each block takes
// the nearest of the eight jittered cell corners.

#include <array>
#include <cstdint>
#include <vector>

namespace perlite::bedrock {

    inline constexpr int kSampleColumns = 4;
    inline constexpr int kSampleSpacing = 8;
    inline int SampleLevels(int height) { return height / kSampleSpacing + 2; }
    inline int SampleIndex(int xi, int zi, int yi, int levels) { return (xi * kSampleColumns + zi) * levels + yi; }

    class BiomeVolume {
    public:
        // samples: 4x4 columns starting 8 blocks west and north of the chunk and 8 below minY, every 8 blocks.
        static BiomeVolume Resolve(const std::vector<int>& samples, int32_t levelSeed, int chunkX, int chunkZ, int minY, int height);
        int IdAt(int x, int y, int z) const;
        // The id when the whole 16-block section holding y has one biome.
        bool UniformAt(int y, int& id) const;

    private:
        int minY = 0;
        struct Section {
            bool uniform = true;
            int id = 0;
            std::vector<int> cells;  // 4096 when mixed: (y << 8) | (z << 4) | x
        };
        std::vector<Section> sections;
    };
}
