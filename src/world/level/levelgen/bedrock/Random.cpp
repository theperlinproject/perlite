#include "world/level/levelgen/bedrock/Random.hpp"

#include <openssl/evp.h>

namespace perlite::bedrock {

    int64_t MixStafford13(int64_t z) {
        uint64_t u = static_cast<uint64_t>(z);
        u = (u ^ (u >> 30)) * static_cast<uint64_t>(-4658895280553007687LL);
        u = (u ^ (u >> 27)) * static_cast<uint64_t>(-7723592293110705685LL);
        return static_cast<int64_t>(u ^ (u >> 31));
    }

    std::array<int64_t, 2> UpgradeSeedTo128(int64_t seed) {
        const int64_t lo = seed ^ 7640891576956012809LL;
        const int64_t hi = WrapAdd(lo, -7046029254386353131LL);
        return {MixStafford13(lo), MixStafford13(hi)};
    }

    std::array<int64_t, 2> SeedHashOf(std::string_view name) {
        uint8_t digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        EVP_Digest(name.data(), name.size(), digest, &length, EVP_md5(), nullptr);
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < 8; i++) {
            lo = (lo << 8) | digest[i];
            hi = (hi << 8) | digest[8 + i];
        }
        return {static_cast<int64_t>(lo), static_cast<int64_t>(hi)};
    }

    int64_t BlockSeed(int32_t x, int32_t y, int32_t z) {
        int64_t seed = static_cast<int64_t>(WrapMul32(x, 3129871)) ^ WrapMul(z, 116129781) ^ static_cast<int64_t>(y);
        seed = WrapAdd(WrapMul(WrapMul(seed, seed), 42317861), WrapMul(seed, 11));
        return seed >> 16;
    }

    int64_t WideBlockSeed(int32_t x, int32_t y, int32_t z) {
        int64_t seed = WrapMul(x, 3129871) ^ WrapMul(z, 116129781) ^ static_cast<int64_t>(y);
        seed = WrapAdd(WrapMul(WrapMul(seed, seed), 42317861), WrapMul(seed, 11));
        return seed >> 16;
    }

    void MersenneTwister::SeedTo(int count) {
        for (int i = seeded; i < count; i++) {
            const uint32_t previous = state[i - 1];
            state[i] = 1812433253u * (previous ^ (previous >> 30)) + static_cast<uint32_t>(i);
        }
        if (count > seeded)
            seeded = count;
    }

    void MersenneTwister::TwistPartial() {
        constexpr int kLazyBlock = 4;
        const int end = std::min(ready + kLazyBlock, kStateSize);
        constexpr int shifted = kStateSize - kShift;
        int needed = ready < shifted ? std::min(end, shifted) + kShift : 0;
        if (end > shifted)
            needed = std::max(needed, end + 1);
        SeedTo(std::min(needed, kStateSize));
        for (int k = ready; k < end; k++)
            TwistWord(k);
        ready = end;
    }

    void MersenneTwister::Refill() {
        if (ready < kStateSize) {
            TwistPartial();
        } else {
            for (int k = 0; k < kStateSize; k++)
                TwistWord(k);
            index = 0;
        }
    }

    float MersenneTwister::NextGaussian() {
        if (spareGaussian) {
            const float s = *spareGaussian;
            spareGaussian.reset();
            return s;
        }
        for (;;) {
            const float first = NextFloat() * 2.0f - 1.0f;
            const float second = NextFloat() * 2.0f - 1.0f;
            const float square = second * second + first * first;
            if (square >= 1.0f || square == 0.0f)
                continue;
            const float multiplier = std::sqrt(std::log(square) * -2.0f / square);
            spareGaussian = second * multiplier;
            return first * multiplier;
        }
    }
}
