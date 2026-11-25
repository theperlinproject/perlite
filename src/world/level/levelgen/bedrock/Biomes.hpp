#pragma once
// Biomes of the Bedrock generator and the climate tree that picks one for a quantised climate point.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace perlite::bedrock {

    struct BiomeInfo {
        const char* name;  // modern identifier without the namespace
        const char* pack;  // behavior-pack identifier (the name the climate table and documents use)
        int id;            // numeric biome id
        bool generated;    // placed by the overworld climate table
    };
    extern const BiomeInfo kBiomeTable[];
    extern const int kBiomeTableSize;

    // Index into kBiomeTable, or -1.
    int BiomeByName(std::string_view nameOrPack);
    int BiomeById(int id);

    using Point = std::array<int64_t, 6>;

    // Nearest climate entry by squared distance to its box, searched over a six-way R-tree built the way the
    // game builds it, so equal distances resolve to the same entry.
    class BiomeTree {
    public:
        struct Entry {
            int biome;  // index into kBiomeTable
            int id;
            Point low, high;
        };
        struct Nearest {
            uint32_t node = 0;
            int64_t distance = 0;
        };

        // The overworld table.
        static const BiomeTree& Overworld();
        explicit BiomeTree(std::vector<Entry> entries);

        Nearest Find(const Point& target, const Nearest* previous) const;
        const Entry& EntryOf(Nearest n) const { return entries[nodes[n.node].entry]; }
        int BiomeId(Nearest n) const { return EntryOf(n).id; }
        const std::vector<Entry>& Entries() const { return entries; }

    private:
        struct Node {
            Point low, high;
            uint32_t entry;  // ~0u for inner nodes
            uint32_t childrenStart = 0, childrenLen = 0;
        };
        void Descend(uint32_t parent, const Point& target, Nearest& best, bool& have) const;
        std::vector<Entry> entries;
        std::vector<Node> nodes;
        std::vector<uint32_t> children;
    };

    // The overworld climate points, worked out from the shared parameter table (see BiomeTree.cpp).
    std::vector<BiomeTree::Entry> OverworldClimateEntries();
}
