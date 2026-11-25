#include "world/level/levelgen/bedrock/BiomeVolume.hpp"

#include <algorithm>
#include <limits>

namespace perlite::bedrock {

    namespace {
        constexpr int kSteps[8][3] = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0, 1, 1}, {1, 0, 0}, {1, 0, 1}, {1, 1, 0}, {1, 1, 1}};

        std::array<float, 3> Jitter(int32_t levelSeed, int x, int y, int z) {
            const int x8 = x >> 3, y8 = y >> 3, z8 = z >> 3;
            const int64_t mixed = static_cast<int64_t>(static_cast<int32_t>((static_cast<uint32_t>(x8) * 3129871u) ^ static_cast<uint32_t>(y8))) ^
                                  static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(z8)) * 116129781ull);
            const uint64_t hashed = (static_cast<uint64_t>(mixed) * 42317861ull + 11ull) * static_cast<uint64_t>(mixed) >> 16;
            uint64_t state = hashed ^ static_cast<uint64_t>(static_cast<uint32_t>(levelSeed)) ^ 0x5DEECE66Dull;
            std::array<float, 3> out{};
            for (float& o : out) {
                state = state * 0x5DEECE66Dull + 0xBull;
                o = (static_cast<float>((state >> 24) & 0xFFFFFFull) * 5.9604645e-08f - 0.5f) * 0.9f;
            }
            return out;
        }

        struct Cell {
            bool uniform;
            int corners[8];
            std::array<float, 3> jitters[8];
        };

        int Nearest(const Cell& c, float fx, float fy, float fz) {
            int best = 0;
            float bestDistance = std::numeric_limits<float>::infinity();
            for (int corner = 0; corner < 8; corner++) {
                const auto& j = c.jitters[corner];
                const float ax = (fx - static_cast<float>(kSteps[corner][0])) + j[0];
                const float ay = j[1] + (fy - static_cast<float>(kSteps[corner][1]));
                const float az = j[2] + (fz - static_cast<float>(kSteps[corner][2]));
                const float d = az * az + ay * ay + ax * ax;
                if (d < bestDistance) {
                    best = corner;
                    bestDistance = d;
                }
            }
            return c.corners[best];
        }
    }

    BiomeVolume BiomeVolume::Resolve(const std::vector<int>& samples, int32_t levelSeed, int chunkX, int chunkZ, int minY, int height) {
        const int levels = SampleLevels(height);
        const int ox = (chunkX << 4) - 8, oy = minY - 8, oz = (chunkZ << 4) - 8;
        auto cellOf = [&](int xc, int zc, int yc) {
            Cell c{};
            for (int i = 0; i < 8; i++)
                c.corners[i] = samples[static_cast<size_t>(SampleIndex(xc + kSteps[i][0], zc + kSteps[i][2], yc + kSteps[i][1], levels))];
            c.uniform = std::all_of(c.corners, c.corners + 8, [&](int v) { return v == c.corners[0]; });
            if (!c.uniform)
                for (int i = 0; i < 8; i++)
                    c.jitters[i] = Jitter(levelSeed, ox + (xc + kSteps[i][0]) * 8, oy + (yc + kSteps[i][1]) * 8, oz + (zc + kSteps[i][2]) * 8);
            return c;
        };
        BiomeVolume v;
        v.minY = minY;
        for (int section = 0; section < height / 16; section++) {
            Cell cells[8];
            for (int i = 0; i < 8; i++) {
                const int hx = (i >> 2) & 1, hy = i & 1, hz = (i >> 1) & 1;
                cells[i] = cellOf(hx + 1, hz + 1, section * 2 + hy + 1);
            }
            Section s;
            const bool allSame = std::all_of(cells, cells + 8, [&](const Cell& c) { return c.uniform && c.corners[0] == cells[0].corners[0]; });
            if (allSame) {
                s.id = cells[0].corners[0];
                v.sections.push_back(std::move(s));
                continue;
            }
            s.uniform = false;
            s.cells.assign(4096, 0);
            for (int i = 0; i < 8; i++) {
                const int hx = (i >> 2) & 1, hy = i & 1, hz = (i >> 1) & 1;
                for (int ly = 0; ly < 8; ly++)
                    for (int lz = 0; lz < 8; lz++)
                        for (int lx = 0; lx < 8; lx++) {
                            const int id = cells[i].uniform ? cells[i].corners[0]
                                                            : Nearest(cells[i], static_cast<float>(lx) * 0.125f, static_cast<float>(ly) * 0.125f,
                                                                      static_cast<float>(lz) * 0.125f);
                            s.cells[static_cast<size_t>(((hy * 8 + ly) << 8) | ((hz * 8 + lz) << 4) | (hx * 8 + lx))] = id;
                        }
            }
            v.sections.push_back(std::move(s));
        }
        return v;
    }

    int BiomeVolume::IdAt(int x, int y, int z) const {
        const int ry = std::clamp(y - minY, 0, static_cast<int>(sections.size()) * 16 - 1);
        const Section& s = sections[static_cast<size_t>(ry >> 4)];
        if (s.uniform)
            return s.id;
        return s.cells[static_cast<size_t>(((ry & 15) << 8) | (std::clamp(z, 0, 15) << 4) | std::clamp(x, 0, 15))];
    }

    bool BiomeVolume::UniformAt(int y, int& id) const {
        const int ry = std::clamp(y - minY, 0, static_cast<int>(sections.size()) * 16 - 1);
        const Section& s = sections[static_cast<size_t>(ry >> 4)];
        if (!s.uniform)
            return false;
        id = s.id;
        return true;
    }
}
