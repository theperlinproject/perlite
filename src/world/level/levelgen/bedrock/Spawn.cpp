#include "world/level/levelgen/bedrock/Spawn.hpp"

#include <cmath>
#include <limits>

namespace perlite::bedrock {

    namespace {
        struct Range {
            int64_t low, high;
        };
        constexpr Range kFull{-10000, 10000};
        constexpr Range kInland{-1099, 10000};
        constexpr Range kTargets[2] = {{-10000, -1599}, {1599, 10000}};

        int64_t Distance(int64_t v, Range r) { return v < r.low ? r.low - v : v > r.high ? v - r.high : 0; }
        int64_t Requantised(int64_t v) {
            return SatCast<int64_t>(static_cast<double>(static_cast<float>(static_cast<double>(v) / 10000.0)) * 10000.0);
        }

        struct Search {
            const BedrockTerrain& terrain;
            int64_t Fitness(int x, int z) const {
                ClimatePoint p = terrain.Column(x, z).Point(0);
                for (auto& v : p)
                    v = Requantised(v);
                int64_t best = std::numeric_limits<int64_t>::max();
                for (const Range& target : kTargets) {
                    const int64_t parts[5] = {Distance(p[0], kFull), Distance(p[1], kFull), Distance(p[2], kInland),
                                              Distance(p[3], kFull), Distance(p[5], target)};
                    int64_t sum = 0;
                    for (int64_t d : parts)
                        sum += d * d;
                    best = std::min(best, sum);
                }
                return best;
            }
            int64_t Score(int x, int z) const {
                return (Fitness(x, z) << 22) + (static_cast<int64_t>(x) * x + static_cast<int64_t>(z) * z);
            }
            void Ring(std::pair<int, int> centre, int step, int limit, std::pair<std::pair<int, int>, int64_t>& best) const {
                int radius = step;
                float angle = 0.0f;
                while (radius <= limit) {
                    const float reach = static_cast<float>(radius);
                    const int x = SatCast(std::sin(angle) * reach) + centre.first;
                    const int z = SatCast(std::cos(angle) * reach) + centre.second;
                    const int64_t score = Score(x, z);
                    if (score < best.second)
                        best = {{x, z}, score};
                    const float next = static_cast<float>(step) / reach + angle;
                    if (next <= 6.2831855f) {
                        angle = next;
                    } else {
                        angle = 0.0f;
                        radius += step;
                    }
                }
            }
        };
    }

    std::pair<int, int> SpawnColumn(const BedrockTerrain& terrain) {
        const Search search{terrain};
        std::pair<std::pair<int, int>, int64_t> best{{0, 0}, search.Fitness(0, 0) << 22};
        search.Ring({0, 0}, 512, 2048, best);
        const auto coarse = best.first;
        search.Ring(coarse, 32, 512, best);
        return best.first;
    }
}
