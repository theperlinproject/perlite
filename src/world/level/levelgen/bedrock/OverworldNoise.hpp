#pragma once
// The single-precision noises of the Bedrock overworld router: improved Perlin noise, its octave sums,
// the paired "normal" noise, and the blended limit/main noise that shapes the terrain grid.

#include "world/level/levelgen/bedrock/Random.hpp"

#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace perlite::bedrock {

    inline float Lerp(float alpha, float p0, float p1) { return p0 + alpha * (p1 - p0); }
    inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    float Gradient(int hash, float x, float y, float z);
    // Xoroshiro's top 53 bits as a double, at the overworld router's exact unit.
    inline double RouterDouble(Xoroshiro& random) {
        return static_cast<double>(static_cast<uint64_t>(random.NextLong()) >> 11) * 1.1102230246251565e-16;
    }

    class ImprovedNoise {
    public:
        explicit ImprovedNoise(Xoroshiro& random);
        float SampleCell(float x, float y, float z, int cellX, int cellY, int cellZ) const;
        std::array<float, 3> origin;

    private:
        int P(int index) const { return perm[static_cast<size_t>(index & 255)]; }
        std::array<uint8_t, 256> perm;
    };

    class MultiOctaveNoise {
    public:
        MultiOctaveNoise(Xoroshiro& random, int firstOctave, std::span<const double> amplitudes);
        float Sample(float x, float y, float z) const;

    private:
        struct Octave {
            ImprovedNoise noise;
            float inputFactor;
            float valueFactor;
        };
        std::vector<Octave> octaves;
    };

    class NormalNoise {
    public:
        NormalNoise(Xoroshiro& random, int firstOctave, std::span<const double> amplitudes);
        NormalNoise(Xoroshiro&& random, int firstOctave, std::span<const double> amplitudes)
            : NormalNoise(random, firstOctave, amplitudes) {}
        float Value(float x, float y, float z) const;

    private:
        MultiOctaveNoise first, second;
        float valueFactor;
    };

    inline constexpr int kGridColumns = 4;

    struct TerrainBuffers {
        std::vector<float> minLimit, maxLimit, main;
    };

    class LegacyBlendedNoise {
    public:
        explicit LegacyBlendedNoise(const PositionalFactory& factory);
        void Fill(TerrainBuffers& buffers, std::array<float, 3> position, int gridLevels) const;

    private:
        struct Level {
            std::array<float, 3> origin;
            std::array<int32_t, 512> perm;
            explicit Level(Xoroshiro& random);
            void ReadArea(float power, float* out, std::array<float, 3> position, std::array<float, 3> scale,
                          int gridLevels) const;
        };
        static void Accumulate(const std::vector<Level>& levels, std::vector<float>& out, std::array<float, 3> position,
                               std::array<float, 3> scale, int gridLevels);
        std::vector<Level> minLimit, maxLimit, main;
    };
}
