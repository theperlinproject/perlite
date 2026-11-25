#include "world/level/levelgen/bedrock/Biomes.hpp"

#include "world/level/levelgen/vanilla/BiomeData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace perlite::bedrock {

    int BiomeByName(std::string_view n) {
        if (n.starts_with("minecraft:"))
            n.remove_prefix(10);
        for (int i = 0; i < kBiomeTableSize; i++)
            if (n == kBiomeTable[i].name || n == kBiomeTable[i].pack)
                return i;
        return -1;
    }

    int BiomeById(int id) {
        for (int i = 0; i < kBiomeTableSize; i++)
            if (kBiomeTable[i].id == id)
                return i;
        return -1;
    }

    std::vector<BiomeTree::Entry> OverworldClimateEntries() {
        // The overworld parameter points are the same boxes as the shared table in vanilla/BiomeData.cpp. Bedrock
        // holds its bounds in single precision and truncates when it quantises them, which moves ten of the
        // boundaries down by one unit; and its dripstone and sulfur caves stop at humidity 0.7, where lush caves
        // begin, instead of overlapping them.
        auto adjust = [](int v) -> int64_t {
            switch (v) {
                case -10500: return -10499;
                case -4500: return -4499;
                case -3500: return -3499;
                case -2225: return -2224;
                case -1900: return -1899;
                case -1100: return -1099;
                case 300: return 299;
                case 4500: return 4499;
                case 7000: return 6999;
                case 9000: return 8999;
                default: return v;
            }
        };
        std::vector<BiomeTree::Entry> out;
        out.reserve(static_cast<size_t>(vanilla::kBiomePointCount));
        for (int i = 0; i < vanilla::kBiomePointCount; i++) {
            const int16_t* p = vanilla::kBiomePointData + static_cast<size_t>(i) * vanilla::kBiomePointStride;
            const char* pack = vanilla::kBiomes[vanilla::kBiomePointBiome[i]].bedrock;
            const int biome = BiomeByName(pack);
            if (biome < 0)
                continue;
            BiomeTree::Entry e{biome, kBiomeTable[biome].id, {}, {}};
            for (int d = 0; d < 6; d++) {
                e.low[static_cast<size_t>(d)] = adjust(p[2 * d]);
                e.high[static_cast<size_t>(d)] = adjust(p[2 * d + 1]);
            }
            const std::string_view name = kBiomeTable[biome].pack;
            if ((name == "dripstone_caves" || name == "sulfur_caves") && e.high[1] == 10000)
                e.high[1] = 6999;
            out.push_back(e);
        }
        return out;
    }

    namespace {
        constexpr int kDims = 6;
        constexpr size_t kBranching = 6;

        int64_t Distance(const Point& low, const Point& high, const Point& t) {
            int64_t sum = 0;
            for (int d = 0; d < kDims; d++) {
                const int64_t v = t[static_cast<size_t>(d)];
                const int64_t gap = v >= high[static_cast<size_t>(d)]  ? v - high[static_cast<size_t>(d)]
                                    : v <= low[static_cast<size_t>(d)] ? low[static_cast<size_t>(d)] - v
                                                                       : 0;
                sum += gap * gap;
            }
            return sum;
        }

        struct Space {
            Point low, high;
        };

        Space Merged(const std::vector<const Space*>& spaces) {
            Space s;
            s.low.fill(std::numeric_limits<int64_t>::max());
            s.high.fill(std::numeric_limits<int64_t>::min());
            for (const Space* sp : spaces)
                for (int d = 0; d < kDims; d++) {
                    s.low[static_cast<size_t>(d)] = std::min(s.low[static_cast<size_t>(d)], sp->low[static_cast<size_t>(d)]);
                    s.high[static_cast<size_t>(d)] = std::max(s.high[static_cast<size_t>(d)], sp->high[static_cast<size_t>(d)]);
                }
            return s;
        }

        int64_t Center(const Space& s, int d, bool absolute) {
            const int64_t v = (s.low[static_cast<size_t>(d)] + s.high[static_cast<size_t>(d)]) / 2;
            return absolute ? (v < 0 ? -v : v) : v;
        }

        // <0, 0, >0 comparing centres from `dimension` on, wrapping through the others.
        int Order(const Space& a, const Space& b, int dimension, bool absolute) {
            for (int step = 0; step < kDims; step++) {
                const int axis = (dimension + step) % kDims;
                const int64_t ca = Center(a, axis, absolute), cb = Center(b, axis, absolute);
                if (ca != cb)
                    return ca < cb ? -1 : 1;
            }
            return 0;
        }

        int64_t Volume(const Space& s) {
            int64_t v = 0;
            for (int d = 0; d < kDims; d++) {
                const int64_t w = s.high[static_cast<size_t>(d)] - s.low[static_cast<size_t>(d)];
                v += w < 0 ? -w : w;
            }
            return v;
        }

        struct Arena {
            std::vector<Space> spaces;
            std::vector<std::optional<uint32_t>> entries;
            std::vector<std::vector<size_t>> children;

            size_t Inner(std::vector<size_t> kids) {
                std::vector<const Space*> ps;
                for (size_t k : kids)
                    ps.push_back(&spaces[k]);
                spaces.push_back(Merged(ps));
                entries.push_back(std::nullopt);
                children.push_back(std::move(kids));
                return spaces.size() - 1;
            }

            std::vector<std::vector<size_t>> Buckets(const std::vector<size_t>& ids) const {
                const double n = static_cast<double>(ids.size());
                const size_t perBucket = static_cast<size_t>(
                    std::pow(static_cast<double>(kBranching), std::floor(std::log(n - 0.01) / std::log(static_cast<double>(kBranching)))));
                std::vector<std::vector<size_t>> buckets;
                std::vector<size_t> current;
                for (size_t id : ids) {
                    current.push_back(id);
                    if (current.size() >= perBucket)
                        buckets.push_back(std::exchange(current, {}));
                }
                if (!current.empty())
                    buckets.push_back(std::move(current));
                return buckets;
            }

            Space BucketSpace(const std::vector<size_t>& bucket) const {
                std::vector<const Space*> ps;
                for (size_t id : bucket)
                    ps.push_back(&spaces[id]);
                return Merged(ps);
            }

            size_t Build(std::vector<size_t> ids) {
                if (ids.size() == 1)
                    return ids[0];
                if (ids.size() <= kBranching)
                    return Inner(std::move(ids));
                bool have = false;
                int64_t bestCost = 0;
                std::vector<std::vector<size_t>> bestBuckets;
                int bestDim = 0;
                for (int d = 0; d < kDims; d++) {
                    std::stable_sort(ids.begin(), ids.end(),
                                     [&](size_t a, size_t b) { return Order(spaces[a], spaces[b], d, false) < 0; });
                    auto buckets = Buckets(ids);
                    int64_t cost = 0;
                    for (const auto& b : buckets)
                        cost += Volume(BucketSpace(b));
                    if (!have || bestCost > cost) {
                        have = true;
                        bestCost = cost;
                        bestBuckets = std::move(buckets);
                        bestDim = d;
                    }
                }
                std::vector<Space> bs;
                for (const auto& b : bestBuckets)
                    bs.push_back(BucketSpace(b));
                std::vector<size_t> ranked(bestBuckets.size());
                for (size_t i = 0; i < ranked.size(); i++)
                    ranked[i] = i;
                std::stable_sort(ranked.begin(), ranked.end(),
                                 [&](size_t a, size_t b) { return Order(bs[a], bs[b], bestDim, true) < 0; });
                std::vector<size_t> kids;
                for (size_t index : ranked)
                    kids.push_back(Build(std::move(bestBuckets[index])));
                return Inner(std::move(kids));
            }

            void Listing(size_t id, size_t depth, std::vector<std::pair<size_t, int64_t>>& out) const {
                out.emplace_back(depth, entries[id] ? static_cast<int64_t>(*entries[id]) : -1);
                for (size_t c : children[id])
                    Listing(c, depth + 1, out);
            }
        };
    }

    BiomeTree::BiomeTree(std::vector<Entry> e) : entries(std::move(e)) {
        Arena arena;
        for (size_t i = 0; i < entries.size(); i++) {
            arena.spaces.push_back(Space{entries[i].low, entries[i].high});
            arena.entries.push_back(static_cast<uint32_t>(i));
            arena.children.emplace_back();
        }
        std::vector<size_t> all(entries.size());
        for (size_t i = 0; i < all.size(); i++)
            all[i] = i;
        const size_t root = arena.Build(std::move(all));
        std::vector<std::pair<size_t, int64_t>> listing;
        arena.Listing(root, 0, listing);

        std::vector<std::vector<uint32_t>> members(listing.size());
        std::vector<uint32_t> path;
        nodes.reserve(listing.size());
        for (size_t index = 0; index < listing.size(); index++) {
            const auto [depth, entry] = listing[index];
            path.resize(std::min(path.size(), depth));
            if (!path.empty())
                members[path.back()].push_back(static_cast<uint32_t>(index));
            Node n;
            if (entry < 0) {
                n.low.fill(std::numeric_limits<int64_t>::max());
                n.high.fill(std::numeric_limits<int64_t>::min());
                n.entry = ~0u;
            } else {
                n.low = entries[static_cast<size_t>(entry)].low;
                n.high = entries[static_cast<size_t>(entry)].high;
                n.entry = static_cast<uint32_t>(entry);
            }
            nodes.push_back(n);
            path.push_back(static_cast<uint32_t>(index));
        }
        for (size_t index = nodes.size(); index-- > 0;)
            for (uint32_t c : members[index])
                for (int d = 0; d < kDims; d++) {
                    nodes[index].low[static_cast<size_t>(d)] = std::min(nodes[index].low[static_cast<size_t>(d)], nodes[c].low[static_cast<size_t>(d)]);
                    nodes[index].high[static_cast<size_t>(d)] = std::max(nodes[index].high[static_cast<size_t>(d)], nodes[c].high[static_cast<size_t>(d)]);
                }
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].childrenStart = static_cast<uint32_t>(children.size());
            nodes[i].childrenLen = static_cast<uint32_t>(members[i].size());
            children.insert(children.end(), members[i].begin(), members[i].end());
        }
    }

    const BiomeTree& BiomeTree::Overworld() {
        static const BiomeTree tree(OverworldClimateEntries());
        return tree;
    }

    BiomeTree::Nearest BiomeTree::Find(const Point& target, const Nearest* previous) const {
        Nearest best;
        bool have = false;
        if (previous) {
            best.node = previous->node;
            best.distance = Distance(nodes[previous->node].low, nodes[previous->node].high, target);
            have = true;
        }
        Descend(0, target, best, have);
        return best;
    }

    void BiomeTree::Descend(uint32_t parent, const Point& target, Nearest& best, bool& have) const {
        const Node& node = nodes[parent];
        for (uint32_t i = 0; i < node.childrenLen; i++) {
            const uint32_t child = children[node.childrenStart + i];
            const Node& c = nodes[child];
            const int64_t d = Distance(c.low, c.high, target);
            if (have && d >= best.distance)
                continue;
            if (c.entry != ~0u) {
                best = Nearest{child, d};
                have = true;
            } else {
                Descend(child, target, best, have);
            }
        }
    }
}
