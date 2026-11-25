// Checks of the Bedrock generator against known outputs: reference MT19937 words, the climate tree against a
// brute-force nearest search, and the spawn column of twelve seeds as the game picks it.

#include "world/level/levelgen/bedrock/Biomes.hpp"
#include "world/level/levelgen/bedrock/Random.hpp"
#include "world/level/levelgen/bedrock/Spawn.hpp"

#include <cstdio>
#include <cstdlib>

using namespace perlite::bedrock;

static int failures = 0;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                           \
            std::printf(__VA_ARGS__);                                                                                  \
            std::printf("\n");                                                                                         \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void Mersenne() {
    MersenneTwister d;
    const uint32_t a = d.NextU32(), b = d.NextU32(), c = d.NextU32();
    CHECK(a == 3499211612u && b == 581869302u && c == 3890346734u, "default words %u %u %u", a, b, c);
    MersenneTwister skip;
    skip.Skip(9999);
    const uint32_t w = skip.NextU32();
    CHECK(w == 4123659995u, "10000th word %u", w);
    CHECK(MersenneTwister(1).NextU32() == 1791095845u, "seed 1");
    // Lazy twisting must hand out the same stream as a full twist, past the first refill.
    for (uint32_t seed : {0u, 1u, 5489u, 0xDEADBEEFu, 0xFFFFFFFFu}) {
        std::array<uint32_t, 624> state{};
        state[0] = seed;
        for (int i = 1; i < 624; i++)
            state[static_cast<size_t>(i)] = 1812433253u * (state[static_cast<size_t>(i - 1)] ^ (state[static_cast<size_t>(i - 1)] >> 30)) + static_cast<uint32_t>(i);
        MersenneTwister lazy(seed);
        bool same = true;
        for (int round = 0; round < 3 && same; round++) {
            for (int k = 0; k < 624; k++) {
                const uint32_t y = (state[static_cast<size_t>(k)] & 0x80000000u) | (state[static_cast<size_t>((k + 1) % 624)] & 0x7FFFFFFFu);
                state[static_cast<size_t>(k)] = state[static_cast<size_t>((k + 397) % 624)] ^ (y >> 1) ^ ((y & 1u) ? 0x9908B0DFu : 0u);
            }
            for (int k = 0; k < 624; k++)
                if (lazy.NextU32() != MersenneTwister::Temper(state[static_cast<size_t>(k)]))
                    same = false;
        }
        CHECK(same, "lazy stream of seed %u", seed);
    }
    CHECK(ChunkSeed(1, 1) == 341872712u + 132899541u, "chunk seed");
    CHECK(RegionSeed(3, 0, 0, 2) == 5u, "region seed");
}

static void Tree() {
    const BiomeTree& tree = BiomeTree::Overworld();
    CHECK(tree.Entries().size() == 7594, "%zu climate entries", tree.Entries().size());
    uint64_t state = 12345;
    auto lcg = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state >> 33;
    };
    BiomeTree::Nearest previous{};
    bool have = false;
    int wrong = 0;
    for (int i = 0; i < 4000; i++) {
        Point t;
        for (auto& v : t)
            v = static_cast<int64_t>(lcg() % 24000) - 12000;
        int64_t brute = INT64_MAX;
        for (const auto& e : tree.Entries()) {
            int64_t sum = 0;
            for (int d = 0; d < 6; d++) {
                const int64_t v = t[static_cast<size_t>(d)];
                const int64_t gap = v >= e.high[static_cast<size_t>(d)] ? v - e.high[static_cast<size_t>(d)]
                                    : v <= e.low[static_cast<size_t>(d)] ? e.low[static_cast<size_t>(d)] - v : 0;
                sum += gap * gap;
            }
            brute = std::min(brute, sum);
        }
        const auto cold = tree.Find(t, nullptr);
        const auto warm = tree.Find(t, have ? &previous : nullptr);
        wrong += cold.distance != brute || warm.distance != brute;
        previous = warm;
        have = true;
    }
    CHECK(wrong == 0, "%d nearest searches missed the closest entry", wrong);
}

static void Spawns() {
    // Spawn columns of a freshly created world, as recorded from the game for these seeds.
    const struct {
        int64_t seed;
        int x, z;
    } spawns[] = {
        {-123456789012LL, 48, 113}, {-1, 498, 387},      {0, -31, 6},     {1, 163, 170},
        {2, -31, 6},                {3, 0, 0},           {42, -31, 6},    {100, 0, 0},
        {777, 0, 0},                {1234567, 25, -31},  {9876543210LL, 0, 0}, {4802433367836389877LL, -75, 127},
    };
    for (const auto& s : spawns) {
        Xoroshiro random(s.seed);
        const PositionalFactory factory = random.ForkPositional();
        const BedrockTerrain terrain(factory, OverworldSettings::For(Preset::Default), DimensionDefinition{});
        const auto [x, z] = SpawnColumn(terrain);
        CHECK(x == s.x && z == s.z, "seed %lld spawn %d,%d expected %d,%d", static_cast<long long>(s.seed), x, z, s.x, s.z);
    }
}

int main() {
    Mersenne();
    Tree();
    Spawns();
    if (failures) {
        std::printf("bedrock_worldgen_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("bedrock_worldgen_test passed\n");
    return 0;
}
