#pragma once
// Random sources of the Bedrock world generator.
//
// Bedrock keeps three: Xoroshiro128++ for the noise router (seeded and forked as positional factories),
// a lazily tempered MT19937 for chunk, region and feature randomness, and the 48-bit LCG some legacy
// placements still use. Every arithmetic step wraps exactly as the game's 32/64-bit integers do.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace perlite::bedrock {

    inline constexpr float kFloatUnit = 5.9604645E-8f;
    inline const double kDoubleUnit = static_cast<double>(1.110223E-16f);

    inline int64_t WrapMul(int64_t a, int64_t b) {
        return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
    }
    inline int64_t WrapAdd(int64_t a, int64_t b) {
        return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    }
    inline int32_t WrapMul32(int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
    }
    inline int32_t WrapAdd32(int32_t a, int32_t b) {
        return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
    }

    // Float to integer conversion as the game's generator code performs it: saturating, NaN to 0.
    template <class T = int32_t, class F>
    inline T SatCast(F v) {
        if (v != v)
            return 0;
        constexpr F lo = static_cast<F>(std::numeric_limits<T>::min());
        constexpr F hi = static_cast<F>(std::numeric_limits<T>::max());
        if (v <= lo)
            return std::numeric_limits<T>::min();
        if (v >= hi)
            return std::numeric_limits<T>::max();
        return static_cast<T>(v);
    }

    int64_t MixStafford13(int64_t z);
    std::array<int64_t, 2> UpgradeSeedTo128(int64_t seed);
    // MD5 of the name, as two big-endian 64-bit halves.
    std::array<int64_t, 2> SeedHashOf(std::string_view name);

    int64_t BlockSeed(int32_t x, int32_t y, int32_t z);
    int64_t WideBlockSeed(int32_t x, int32_t y, int32_t z);

    class PositionalFactory;

    class Xoroshiro {
    public:
        explicit Xoroshiro(int64_t seed) {
            const auto s = UpgradeSeedTo128(seed);
            Set(s[0], s[1]);
        }
        static Xoroshiro FromParts(int64_t lo, int64_t hi) {
            Xoroshiro x;
            x.Set(lo, hi);
            return x;
        }

        int64_t NextLong() {
            const uint64_t s0 = static_cast<uint64_t>(lo);
            uint64_t s1 = static_cast<uint64_t>(hi);
            const uint64_t result = std::rotl(s0 + s1, 17) + s0;
            s1 ^= s0;
            lo = static_cast<int64_t>(std::rotl(s0, 49) ^ s1 ^ (s1 << 21));
            hi = static_cast<int64_t>(std::rotl(s1, 28));
            return static_cast<int64_t>(result);
        }
        int32_t NextInt() { return static_cast<int32_t>(NextLong()); }
        int32_t NextInt(int32_t bound) {
            const uint64_t bound64 = static_cast<uint64_t>(static_cast<int64_t>(bound));
            uint64_t bits = static_cast<uint32_t>(NextInt());
            uint64_t multiplied = bits * bound64;
            uint64_t fractional = multiplied & 0xFFFFFFFFull;
            if (fractional < bound64) {
                const uint64_t threshold = static_cast<uint32_t>(-static_cast<uint32_t>(bound)) % static_cast<uint32_t>(bound);
                while (fractional < threshold) {
                    bits = static_cast<uint32_t>(NextInt());
                    multiplied = bits * bound64;
                    fractional = multiplied & 0xFFFFFFFFull;
                }
            }
            return static_cast<int32_t>(multiplied >> 32);
        }
        float NextFloat() { return static_cast<float>(RawBits(24)) * kFloatUnit; }
        double NextDouble() { return static_cast<double>(RawBits(53)) * kDoubleUnit; }
        int32_t NextBits(int bits) { return static_cast<int32_t>(static_cast<uint64_t>(NextLong()) >> (64 - bits)); }

        PositionalFactory ForkPositional();

    private:
        Xoroshiro() = default;
        void Set(int64_t l, int64_t h) {
            if ((l | h) == 0) {
                lo = -7046029254386353131LL;
                hi = 7640891576956012809LL;
            } else {
                lo = l;
                hi = h;
            }
        }
        int64_t RawBits(int bits) { return static_cast<int64_t>(static_cast<uint64_t>(NextLong()) >> (64 - bits)); }
        int64_t lo = 0, hi = 0;
    };

    class PositionalFactory {
    public:
        PositionalFactory() = default;
        PositionalFactory(int64_t lo, int64_t hi) : lo(lo), hi(hi) {}
        Xoroshiro At(int32_t x, int32_t y, int32_t z) const { return Xoroshiro::FromParts(BlockSeed(x, y, z) ^ lo, hi); }
        Xoroshiro AtWide(int32_t x, int32_t y, int32_t z) const {
            return Xoroshiro::FromParts(WideBlockSeed(x, y, z) ^ lo, hi);
        }
        Xoroshiro HashOf(std::string_view name) const {
            const auto h = SeedHashOf(name);
            return Xoroshiro::FromParts(h[0] ^ lo, h[1] ^ hi);
        }

    private:
        int64_t lo = 0, hi = 0;
    };

    inline PositionalFactory Xoroshiro::ForkPositional() {
        const int64_t a = NextLong();
        const int64_t b = NextLong();
        return PositionalFactory(a, b);
    }

    // MT19937 that twists only the words it is about to hand out, so seeding one for a few draws (as the
    // generator does per chunk and per feature) costs a handful of words rather than the whole state.
    class MersenneTwister {
    public:
        static constexpr int kStateSize = 624;
        static constexpr int kShift = 397;

        explicit MersenneTwister(uint32_t seed = 5489u) : seed(seed) { state[0] = seed; }

        uint32_t Seed() const { return seed; }
        uint32_t NextU32() {
            if (index >= ready)
                Refill();
            return Temper(state[index++]);
        }
        void Skip(int count) {
            for (int i = 0; i < count; i++)
                NextU32();
        }
        int32_t NextInt() { return static_cast<int32_t>(NextU32() >> 1); }
        int32_t NextInt(int32_t bound) {
            return bound == 0 ? 0 : static_cast<int32_t>(NextU32() % static_cast<uint32_t>(bound));
        }
        bool NextBoolean() { return ((NextU32() >> 27) & 1u) != 0; }
        static float FloatFromWord(uint32_t word) { return static_cast<float>(static_cast<double>(word) * kUnit32); }
        float NextFloat() { return FloatFromWord(NextU32()); }
        double NextDouble() { return static_cast<double>(NextU32()) * kUnit32; }
        float NextGaussian();

        // The first COUNT words a generator seeded with `seed` hands out, without building its state.
        template <int COUNT>
        static std::array<uint32_t, COUNT> LeadingWords(uint32_t seed) {
            std::array<uint32_t, kShift + COUNT> st{};
            st[0] = seed;
            for (int i = 1; i < kShift + COUNT; i++)
                st[static_cast<size_t>(i)] = 1812433253u * (st[static_cast<size_t>(i - 1)] ^ (st[static_cast<size_t>(i - 1)] >> 30)) + static_cast<uint32_t>(i);
            std::array<uint32_t, COUNT> out{};
            for (int k = 0; k < COUNT; k++) {
                const uint32_t y = (st[static_cast<size_t>(k)] & 0x80000000u) | (st[static_cast<size_t>(k + 1)] & 0x7FFFFFFFu);
                const uint32_t mag = (y & 1u) ? 0x9908B0DFu : 0u;
                out[static_cast<size_t>(k)] = Temper(st[static_cast<size_t>(k + kShift)] ^ (y >> 1) ^ mag);
            }
            return out;
        }

        static uint32_t Temper(uint32_t y) {
            y ^= y >> 11;
            y ^= (y << 7) & 0x9D2C5680u;
            y ^= (y << 15) & 0xEFC60000u;
            return y ^ (y >> 18);
        }

    private:
        static constexpr double kUnit32 = 2.3283064365386963e-10;
        void SeedTo(int count);
        void TwistWord(int k) {
            const uint32_t y = (state[k] & 0x80000000u) | (state[(k + 1) % kStateSize] & 0x7FFFFFFFu);
            const uint32_t mag = (y & 1u) ? 0x9908B0DFu : 0u;
            state[k] = state[(k + kShift) % kStateSize] ^ (y >> 1) ^ mag;
        }
        void TwistPartial();
        void Refill();

        uint32_t seed;
        std::array<uint32_t, kStateSize> state{};
        int seeded = 1;
        int ready = 0;
        int index = 0;
        std::optional<float> spareGaussian;
    };

    // java.util.Random's 48-bit generator.
    class LegacyRandom {
    public:
        explicit LegacyRandom(int64_t seed) : seed((seed ^ kMultiplier) & kMask) {}
        int32_t NextBits(int bits) {
            seed = WrapAdd(WrapMul(seed, kMultiplier), 11) & kMask;
            return static_cast<int32_t>(seed >> (48 - bits));
        }
        int64_t NextLong() {
            const int64_t upper = NextBits(32);
            const int64_t lower = NextBits(32);
            return WrapAdd(static_cast<int64_t>(static_cast<uint64_t>(upper) << 32), lower);
        }
        int32_t NextInt() { return NextBits(32); }
        int32_t NextInt(int32_t bound) {
            if ((bound & (bound - 1)) == 0)
                return static_cast<int32_t>((static_cast<int64_t>(bound) * NextBits(31)) >> 31);
            for (;;) {
                const int32_t sample = NextBits(31);
                const int32_t modulo = sample % bound;
                if (WrapAdd32(WrapAdd32(sample, -modulo), bound - 1) >= 0)
                    return modulo;
            }
        }
        float NextFloat() { return static_cast<float>(NextBits(24)) * kFloatUnit; }
        double NextDouble() {
            const int64_t upper = NextBits(26);
            const int64_t lower = NextBits(27);
            return static_cast<double>((upper << 27) + lower) * kDoubleUnit;
        }

    private:
        static constexpr int64_t kMultiplier = 25214903917LL;
        static constexpr int64_t kMask = (int64_t(1) << 48) - 1;
        int64_t seed;
    };

    inline int32_t LevelSeed32(int64_t seed) { return static_cast<int32_t>(seed); }
    inline uint32_t ChunkSeed(int32_t chunkX, int32_t chunkZ) {
        return static_cast<uint32_t>(WrapAdd32(WrapMul32(chunkX, 341872712), WrapMul32(chunkZ, 132899541)));
    }
    inline MersenneTwister ChunkRandom(int32_t chunkX, int32_t chunkZ) { return MersenneTwister(ChunkSeed(chunkX, chunkZ)); }
    inline uint32_t RegionSeed(uint32_t levelSeed, int32_t regionX, int32_t regionZ, int32_t salt) {
        return static_cast<uint32_t>(WrapAdd32(
            WrapAdd32(WrapAdd32(WrapMul32(regionZ, -245998635), WrapMul32(regionX, -1724254968)), static_cast<int32_t>(levelSeed)),
            salt));
    }
    inline MersenneTwister MaterialAdjustmentRandom(int64_t levelSeed) {
        MersenneTwister r(static_cast<uint32_t>(LevelSeed32(levelSeed)));
        r.Skip(20202);
        return r;
    }

    // Per-world multipliers that spread chunk coordinates into large-feature and decoration seeds.
    inline std::pair<uint32_t, uint32_t> LargeFeatureMultipliers(uint32_t levelSeed) {
        const auto w = MersenneTwister::LeadingWords<2>(levelSeed);
        return {(w[0] >> 1) | 1u, (w[1] >> 1) | 1u};
    }
    inline uint32_t DecorationSeed(uint32_t levelSeed, std::pair<uint32_t, uint32_t> m, int32_t chunkX, int32_t chunkZ) {
        return levelSeed ^ (static_cast<uint32_t>(chunkZ) * m.second + static_cast<uint32_t>(chunkX) * m.first);
    }

    inline constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
    inline constexpr uint64_t kFnvPrime = 0x00000100000001b3ull;
    inline uint64_t FnvMix(uint64_t hash, uint64_t value) { return (hash ^ value) * kFnvPrime; }
}
