#include "world/level/levelgen/bedrock/OverworldNoise.hpp"

#include <string>

namespace perlite::bedrock {

    namespace {
        constexpr float kGradients[16][3] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
            {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1},
        };

        std::array<uint8_t, 256> ShuffledPermutation(Xoroshiro& random) {
            std::array<uint8_t, 256> p{};
            for (int i = 0; i < 256; i++)
                p[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
            for (int i = 0; i < 256; i++) {
                const int j = random.NextInt(256 - i) + i;
                std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(j)]);
            }
            return p;
        }
    }

    float Gradient(int hash, float x, float y, float z) {
        const float* d = kGradients[hash & 15];
        return d[0] * x + d[1] * y + d[2] * z;
    }

    ImprovedNoise::ImprovedNoise(Xoroshiro& random) {
        for (float& o : origin)
            o = static_cast<float>(RouterDouble(random) * 256.0);
        perm = ShuffledPermutation(random);
    }

    float ImprovedNoise::SampleCell(float x, float y, float z, int cellX, int cellY, int cellZ) const {
        const int a = P(cellX) + cellY;
        const int b = P(cellX + 1) + cellY;
        const int aa = P(a) + cellZ;
        const int ab = P(a + 1) + cellZ;
        const int ba = P(b) + cellZ;
        const int bb = P(b + 1) + cellZ;
        const float u = Fade(x), v = Fade(y), w = Fade(z);
        const float x1 = Lerp(u, Gradient(P(aa), x, y, z), Gradient(P(ba), x - 1.0f, y, z));
        const float x2 = Lerp(u, Gradient(P(ab), x, y - 1.0f, z), Gradient(P(bb), x - 1.0f, y - 1.0f, z));
        const float x3 = Lerp(u, Gradient(P(aa + 1), x, y, z - 1.0f), Gradient(P(ba + 1), x - 1.0f, y, z - 1.0f));
        const float x4 =
            Lerp(u, Gradient(P(ab + 1), x, y - 1.0f, z - 1.0f), Gradient(P(bb + 1), x - 1.0f, y - 1.0f, z - 1.0f));
        return Lerp(w, Lerp(v, x1, x2), Lerp(v, x3, x4));
    }

    MultiOctaveNoise::MultiOctaveNoise(Xoroshiro& random, int firstOctave, std::span<const double> amplitudes) {
        const int count = static_cast<int>(amplitudes.size());
        const PositionalFactory factory = random.ForkPositional();
        const float denominator = static_cast<float>((1u << count) - 1);
        for (int i = 0; i < count; i++) {
            const double amplitude = amplitudes[static_cast<size_t>(i)];
            if (amplitude == 0.0)
                continue;
            const int octave = firstOctave + i;
            const float valueFactor = static_cast<float>(1u << (count - 1 - i)) / denominator;
            Xoroshiro r = factory.HashOf("octave_" + std::to_string(octave));
            octaves.push_back(Octave{ImprovedNoise(r), static_cast<float>(std::pow(2.0, octave)),
                                     valueFactor * static_cast<float>(amplitude)});
        }
    }

    float MultiOctaveNoise::Sample(float x, float y, float z) const {
        float sum = 0.0f;
        for (const Octave& o : octaves) {
            const float fx = x * o.inputFactor + o.noise.origin[0];
            const float fy = y * o.inputFactor + o.noise.origin[1];
            const float fz = z * o.inputFactor + o.noise.origin[2];
            const float floorX = std::floor(fx), floorY = std::floor(fy), floorZ = std::floor(fz);
            sum += o.noise.SampleCell(fx - floorX, fy - floorY, fz - floorZ, SatCast(floorX), SatCast(floorY),
                                      SatCast(floorZ)) *
                   o.valueFactor;
        }
        return sum;
    }

    namespace {
        float NormalValueFactor(std::span<const double> amplitudes) {
            int lo = -1, hi = -1;
            for (int i = 0; i < static_cast<int>(amplitudes.size()); i++)
                if (amplitudes[static_cast<size_t>(i)] != 0.0) {
                    if (lo < 0)
                        lo = i;
                    hi = i;
                }
            const float span = lo < 0 ? 2.0f : static_cast<float>(hi - lo) + 1.0f;
            return 0.16666667f / ((1.0f / span + 1.0f) * 0.1f);
        }
    }

    NormalNoise::NormalNoise(Xoroshiro& random, int firstOctave, std::span<const double> amplitudes)
        : first(random, firstOctave, amplitudes), second(random, firstOctave, amplitudes),
          valueFactor(NormalValueFactor(amplitudes)) {}

    float NormalNoise::Value(float x, float y, float z) const {
        constexpr float k = 1.0181268f;
        return (first.Sample(x, y, z) + second.Sample(x * k, y * k, z * k)) * valueFactor;
    }

    LegacyBlendedNoise::Level::Level(Xoroshiro& random) {
        for (float& o : origin)
            o = static_cast<float>(RouterDouble(random)) * 256.0f;
        perm.fill(0);
        for (int i = 0; i < 256; i++)
            perm[static_cast<size_t>(i)] = i;
        for (int i = 0; i < 256; i++) {
            const int j = random.NextInt(256 - i) + i;
            std::swap(perm[static_cast<size_t>(i)], perm[static_cast<size_t>(j)]);
        }
        for (int i = 0; i < 256; i++)
            perm[static_cast<size_t>(i + 256)] = perm[static_cast<size_t>(i)];
    }

    void LegacyBlendedNoise::Level::ReadArea(float power, float* out, std::array<float, 3> position,
                                             std::array<float, 3> scale, int gridLevels) const {
        struct Row {
            int y;
            float fadeT;
            bool special;
            float gy;
            int first[2], second[2];
        };
        thread_local std::vector<Row> rows;
        const float inverse = 1.0f / power;
        auto hash = [&](int index) { return perm[static_cast<size_t>(index)]; };
        const float yStart = 0.0f * scale[1] + origin[1];
        int cachedY = -1;
        float cache[4] = {0, 0, 0, 0};
        size_t index = 0;
        for (int i = 0; i < kGridColumns; i++) {
            const float fx = (static_cast<float>(i) + position[0]) * scale[0] + origin[0];
            const float floorX = std::floor(fx);
            const int x = SatCast(floorX) & 255;
            const float u = fx - floorX;
            const float fu = Fade(u);
            rows.clear();
            for (int j = 0; j < gridLevels; j++) {
                const float fy = (static_cast<float>(j) + position[1]) * scale[1] + origin[1];
                const float floorY = std::floor(fy);
                const int y = SatCast(floorY) & 255;
                const float t = fy - floorY;
                const bool special = SatCast(yStart) == SatCast(fy);
                const float gy = special ? yStart - static_cast<float>(SatCast(yStart))
                                         : t - std::floor(t / scale[1]) * scale[1];
                const int a = perm[static_cast<size_t>(x)] + y;
                const int b = perm[static_cast<size_t>(x) + 1] + y;
                rows.push_back(Row{y, Fade(t), special, gy, {hash(a), hash(b)}, {hash(a + 1), hash(b + 1)}});
            }
            for (int k = 0; k < kGridColumns; k++) {
                const float fz = (static_cast<float>(k) + position[2]) * scale[2] + origin[2];
                const float floorZ = std::floor(fz);
                const int z = SatCast(floorZ) & 255;
                const float w = fz - floorZ;
                const float fadeW = Fade(w);
                for (const Row& row : rows) {
                    if (row.special || row.y != cachedY) {
                        const float gy = row.gy;
                        const int ha = row.first[0], hb = row.first[1];
                        const int ha1 = row.second[0], hb1 = row.second[1];
                        cache[0] = Lerp(fu, Gradient(hash(ha + z), u, gy, w), Gradient(hash(hb + z), u - 1.0f, gy, w));
                        cache[1] = Lerp(fu, Gradient(hash(ha1 + z), u, gy - 1.0f, w),
                                        Gradient(hash(hb1 + z), u - 1.0f, gy - 1.0f, w));
                        cache[2] = Lerp(fu, Gradient(hash(ha + z + 1), u, gy, w - 1.0f),
                                        Gradient(hash(hb + z + 1), u - 1.0f, gy, w - 1.0f));
                        cache[3] = Lerp(fu, Gradient(hash(ha1 + z + 1), u, gy - 1.0f, w - 1.0f),
                                        Gradient(hash(hb1 + z + 1), u - 1.0f, gy - 1.0f, w - 1.0f));
                        cachedY = row.y;
                    }
                    const float v0 = Lerp(row.fadeT, cache[0], cache[1]);
                    const float v1 = Lerp(row.fadeT, cache[2], cache[3]);
                    out[index] += Lerp(fadeW, v0, v1) * inverse;
                    index++;
                }
            }
        }
    }

    LegacyBlendedNoise::LegacyBlendedNoise(const PositionalFactory& factory) {
        Xoroshiro random = factory.HashOf("minecraft:terrain");
        auto levels = [&](int count) {
            std::vector<Level> v;
            for (int i = 0; i < count; i++)
                v.emplace_back(random);
            return v;
        };
        minLimit = levels(16);
        maxLimit = levels(16);
        main = levels(8);
    }

    void LegacyBlendedNoise::Accumulate(const std::vector<Level>& levels, std::vector<float>& out,
                                        std::array<float, 3> position, std::array<float, 3> scale, int gridLevels) {
        std::fill(out.begin(), out.end(), 0.0f);
        float frequency = 1.0f;
        for (const Level& level : levels) {
            level.ReadArea(frequency, out.data(), position,
                           {scale[0] * frequency, scale[1] * frequency, scale[2] * frequency}, gridLevels);
            frequency *= 0.5f;
        }
    }

    void LegacyBlendedNoise::Fill(TerrainBuffers& buffers, std::array<float, 3> position, int gridLevels) const {
        const size_t size = static_cast<size_t>(kGridColumns * kGridColumns * gridLevels);
        buffers.minLimit.resize(size);
        buffers.maxLimit.resize(size);
        buffers.main.resize(size);
        constexpr float limit = 684.412f;
        Accumulate(minLimit, buffers.minLimit, position, {limit, limit, limit}, gridLevels);
        Accumulate(maxLimit, buffers.maxLimit, position, {limit, limit, limit}, gridLevels);
        Accumulate(main, buffers.main, position, {8.55515f, 4.277575f, 8.55515f}, gridLevels);
    }
}
