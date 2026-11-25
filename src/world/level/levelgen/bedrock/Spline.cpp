#include "world/level/levelgen/bedrock/Spline.hpp"

#include <algorithm>
#include <cmath>

namespace perlite::bedrock {

    namespace {
        float Lerpf(float t, float a, float b) { return a + t * (b - a); }

        class Builder {
        public:
            Builder(SplineCoordinate coordinate, LeafTransform leaves) : coordinate(coordinate), leaves(leaves) {}
            Builder& Point(float location, Spline value, float derivative = 0.0f) {
                if (value.IsConstant())
                    value = Spline(leaves.Apply(value.Constant()));
                locations.push_back(location);
                values.push_back(std::move(value));
                derivatives.push_back(derivative);
                return *this;
            }
            Spline Build() { return Spline(coordinate, locations, values, derivatives); }

        private:
            SplineCoordinate coordinate;
            LeafTransform leaves;
            std::vector<float> locations;
            std::vector<Spline> values;
            std::vector<float> derivatives;
        };

        Builder Make(SplineCoordinate c, LeafTransform leaves) { return Builder(c, leaves); }
        using C = SplineCoordinate;

        Spline WeirdnessJaggedness(float factor, LeafTransform leaves) {
            return Make(C::Weirdness, leaves).Point(-0.01f, 0.63f * factor).Point(0.01f, 0.3f * factor).Build();
        }

        Spline RidgeJaggedness(float atPeakRidge, float atHighRidge, LeafTransform leaves) {
            const float start = PeaksAndValleys(0.4f);
            const float end = PeaksAndValleys(0.56666666f);
            const float middle = (start + end) / 2.0f;
            Builder ridge = Make(C::Ridges, leaves);
            ridge.Point(start, 0.0f);
            if (atHighRidge > 0.0f)
                ridge.Point(middle, WeirdnessJaggedness(atHighRidge, leaves));
            else
                ridge.Point(middle, 0.0f);
            if (atPeakRidge > 0.0f)
                ridge.Point(1.0f, WeirdnessJaggedness(atPeakRidge, leaves));
            else
                ridge.Point(1.0f, 0.0f);
            return ridge.Build();
        }

        Spline ErosionJaggedness(float peak0, float peak1, float high0, float high1, LeafTransform leaves) {
            const Spline at0 = RidgeJaggedness(peak0, high0, leaves);
            const Spline at1 = RidgeJaggedness(peak1, high1, leaves);
            return Make(C::Erosion, leaves).Point(-1.0f, at0).Point(-0.78f, at1).Point(-0.5775f, at1).Point(-0.375f, 0.0f).Build();
        }

        Spline ErosionFactor(float baseValue, bool shattered, LeafTransform leaves) {
            const Spline base = Make(C::Weirdness, leaves).Point(-0.2f, 6.3f).Point(0.2f, baseValue).Build();
            Builder erosion = Make(C::Erosion, leaves);
            erosion.Point(-0.6f, base)
                .Point(-0.5f, Make(C::Weirdness, leaves).Point(-0.05f, 6.3f).Point(0.05f, 2.67f).Build())
                .Point(-0.35f, base)
                .Point(-0.25f, base)
                .Point(-0.1f, Make(C::Weirdness, leaves).Point(-0.05f, 2.67f).Point(0.05f, 6.3f).Build())
                .Point(0.03f, base);
            if (shattered) {
                const Spline weirdnessShattered = Make(C::Weirdness, leaves).Point(0.0f, baseValue).Point(0.1f, 0.625f).Build();
                const Spline ridgesShattered =
                    Make(C::Ridges, leaves).Point(-0.9f, baseValue).Point(-0.69f, weirdnessShattered).Build();
                return erosion.Point(0.35f, baseValue).Point(0.45f, ridgesShattered).Point(0.55f, ridgesShattered).Point(0.62f, baseValue).Build();
            }
            const Spline extremeHills = Make(C::Ridges, leaves).Point(-0.7f, base).Point(-0.15f, 1.37f).Build();
            const Spline peaksOnly = Make(C::Ridges, leaves).Point(0.45f, base).Point(0.7f, 1.56f).Build();
            return erosion.Point(0.05f, peaksOnly).Point(0.4f, peaksOnly).Point(0.45f, extremeHills).Point(0.55f, extremeHills).Point(0.58f, baseValue).Build();
        }

        float Slope(float y1, float y2, float x1, float x2) { return (y2 - y1) / (x2 - x1); }

        float MountainContinentalness(float ridge, float modulation, float allowRiversBelow) {
            const float ridgeSlope = 1.0f - (1.0f - modulation) * 0.5f;
            const float ridgeIntersect = 0.5f * (1.0f - modulation);
            const float adjusted = (ridge + 1.17f) * 0.46082947f;
            const float c = adjusted * ridgeSlope - ridgeIntersect;
            return ridge < allowRiversBelow ? std::max(c, -0.2222f) : std::max(c, 0.0f);
        }

        float MountainRidgeZeroPoint(float modulation) {
            const float ridgeSlope = 1.0f - (1.0f - modulation) * 0.5f;
            const float ridgeIntersect = 0.5f * (1.0f - modulation);
            return ridgeIntersect / (0.46082947f * ridgeSlope) - 1.17f;
        }

        Spline MountainRidge(float modulation, bool saddle, LeafTransform leaves) {
            Builder spline = Make(C::Ridges, leaves);
            const float minC = MountainContinentalness(-1.0f, modulation, -0.7f);
            const float maxC = MountainContinentalness(1.0f, modulation, -0.7f);
            const float zero = MountainRidgeZeroPoint(modulation);
            if (-0.65f < zero && zero < 1.0f) {
                const float afterRiver = MountainContinentalness(-0.65f, modulation, -0.7f);
                const float beforeRiver = MountainContinentalness(-0.75f, modulation, -0.7f);
                const float minDerivative = Slope(minC, beforeRiver, -1.0f, -0.75f);
                const float zeroC = MountainContinentalness(zero, modulation, -0.7f);
                const float maxDerivative = Slope(zeroC, maxC, zero, 1.0f);
                return spline.Point(-1.0f, minC, minDerivative)
                    .Point(-0.75f, beforeRiver)
                    .Point(-0.65f, afterRiver)
                    .Point(zero - 0.01f, zeroC)
                    .Point(zero, zeroC, maxDerivative)
                    .Point(1.0f, maxC, maxDerivative)
                    .Build();
            }
            const float simple = Slope(minC, maxC, -1.0f, 1.0f);
            if (saddle)
                spline.Point(-1.0f, std::max(0.2f, minC)).Point(0.0f, Lerpf(0.5f, minC, maxC), simple);
            else
                spline.Point(-1.0f, minC, simple);
            return spline.Point(1.0f, maxC, simple).Build();
        }

        Spline RidgeSpline(float valley, float low, float mid, float high, float peaks, float minValleySteepness,
                           LeafTransform leaves) {
            const float d1 = std::max(0.5f * (low - valley), minValleySteepness);
            const float d2 = 5.0f * (mid - low);
            return Make(C::Ridges, leaves)
                .Point(-1.0f, valley, d1)
                .Point(-0.4f, low, std::min(d1, d2))
                .Point(0.0f, mid, d2)
                .Point(0.4f, high, 2.0f * (high - mid))
                .Point(1.0f, peaks, 0.7f * (peaks - high))
                .Build();
        }

        Spline ErosionOffset(float lowValley, float hill, float tallHill, float mountainFactor, float plain, float swamp,
                             bool extremeHills, bool saddle, LeafTransform leaves) {
            const Spline veryLow = MountainRidge(Lerpf(mountainFactor, 0.6f, 1.5f), saddle, leaves);
            const Spline low = MountainRidge(Lerpf(mountainFactor, 0.6f, 1.0f), saddle, leaves);
            const Spline mountains = MountainRidge(mountainFactor, saddle, leaves);
            const Spline widePlateau = RidgeSpline(lowValley - 0.15f, 0.5f * mountainFactor, Lerpf(0.5f, 0.5f, 0.5f) * mountainFactor,
                                                   0.5f * mountainFactor, 0.6f * mountainFactor, 0.5f, leaves);
            const Spline narrowPlateau = RidgeSpline(lowValley, plain * mountainFactor, hill * mountainFactor,
                                                     0.5f * mountainFactor, 0.6f * mountainFactor, 0.5f, leaves);
            const Spline plains = RidgeSpline(lowValley, plain, plain, hill, tallHill, 0.5f, leaves);
            const Spline plainsFarInland = RidgeSpline(lowValley, plain, plain, hill, tallHill, 0.5f, leaves);
            const Spline hills = Make(C::Ridges, leaves).Point(-1.0f, lowValley).Point(-0.4f, plains).Point(0.0f, tallHill + 0.07f).Build();
            const Spline swamps = RidgeSpline(-0.02f, swamp, swamp, hill, tallHill, 0.0f, leaves);
            Builder erosion = Make(C::Erosion, leaves);
            erosion.Point(-0.85f, veryLow).Point(-0.7f, low).Point(-0.4f, mountains).Point(-0.35f, widePlateau)
                .Point(-0.1f, narrowPlateau).Point(0.2f, plains);
            if (extremeHills)
                erosion.Point(0.4f, plainsFarInland).Point(0.45f, hills).Point(0.55f, hills).Point(0.58f, plainsFarInland);
            return erosion.Point(0.7f, swamps).Build();
        }
    }

    float Spline::Sample(const SplineInput& input) const {
        if (!multipoint)
            return constant;
        const float x = input.Get(coordinate);
        const int start = static_cast<int>(std::partition_point(locations.begin(), locations.end(),
                                                                [&](float l) { return l <= x; }) -
                                           locations.begin()) -
                          1;
        const size_t last = locations.size() - 1;
        auto extend = [&](float value, size_t index) {
            const float d = derivatives[index];
            return d == 0.0f ? value : value + d * (x - locations[index]);
        };
        if (start < 0)
            return extend(values[0].Sample(input), 0);
        const size_t s = static_cast<size_t>(start);
        if (s == last)
            return extend(values[last].Sample(input), last);
        const float x1 = locations[s], x2 = locations[s + 1];
        const float t = (x - x1) / (x2 - x1);
        const float y1 = values[s].Sample(input);
        const float y2 = values[s + 1].Sample(input);
        const float a = derivatives[s] * (x2 - x1) - (y2 - y1);
        const float b = -derivatives[s + 1] * (x2 - x1) + (y2 - y1);
        return Lerpf(t, y1, y2) + t * (1.0f - t) * Lerpf(t, a, b);
    }

    float PeaksAndValleys(float weirdness) {
        return -(std::fabs(std::fabs(weirdness) - 0.6666667f) - 0.33333334f) * 3.0f;
    }

    Spline OverworldOffset(LeafTransform leaves) {
        const Spline beach = ErosionOffset(-0.15f, 0.0f, 0.0f, 0.1f, 0.0f, -0.03f, false, false, leaves);
        const Spline low = ErosionOffset(-0.1f, 0.03f, 0.1f, 0.1f, 0.01f, -0.03f, false, false, leaves);
        const Spline mid = ErosionOffset(-0.1f, 0.03f, 0.1f, 0.7f, 0.01f, -0.03f, true, true, leaves);
        const Spline high = ErosionOffset(-0.05f, 0.03f, 0.1f, 1.0f, 0.01f, 0.01f, true, true, leaves);
        return Make(C::Continents, leaves)
            .Point(-1.1f, 0.044f)
            .Point(-1.02f, -0.2222f)
            .Point(-0.51f, -0.2222f)
            .Point(-0.44f, -0.12f)
            .Point(-0.18f, -0.12f)
            .Point(-0.16f, beach)
            .Point(-0.15f, beach)
            .Point(-0.1f, low)
            .Point(0.25f, mid)
            .Point(1.0f, high)
            .Build();
    }

    Spline OverworldFactor(LeafTransform leaves) {
        return Make(C::Continents, LeafTransform::Identity())
            .Point(-0.19f, 3.95f)
            .Point(-0.15f, ErosionFactor(6.25f, true, LeafTransform::Identity()))
            .Point(-0.1f, ErosionFactor(5.47f, true, leaves))
            .Point(0.03f, ErosionFactor(5.08f, true, leaves))
            .Point(0.06f, ErosionFactor(4.69f, false, leaves))
            .Build();
    }

    Spline OverworldJaggedness(LeafTransform leaves) {
        return Make(C::Continents, leaves)
            .Point(-0.11f, 0.0f)
            .Point(0.03f, ErosionJaggedness(1.0f, 0.5f, 0.0f, 0.0f, leaves))
            .Point(0.65f, ErosionJaggedness(1.0f, 1.0f, 1.0f, 0.0f, leaves))
            .Build();
    }
}
