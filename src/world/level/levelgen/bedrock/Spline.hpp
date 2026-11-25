#pragma once
// Cubic terrain splines of the overworld router (offset, factor and jaggedness), built in the game's
// float arithmetic so every knot lands on the same value.

#include <memory>
#include <vector>

namespace perlite::bedrock {

    enum class SplineCoordinate { Continents, Erosion, Weirdness, Ridges };

    struct SplineInput {
        float continents, erosion, weirdness, ridges;
        float Get(SplineCoordinate c) const {
            switch (c) {
                case SplineCoordinate::Continents: return continents;
                case SplineCoordinate::Erosion: return erosion;
                case SplineCoordinate::Weirdness: return weirdness;
                case SplineCoordinate::Ridges: return ridges;
            }
            return 0.0f;
        }
    };

    // A value applied to every constant leaf of a spline (the amplified preset scales them).
    struct LeafTransform {
        enum class Kind { Identity, Scale, Hyperbola } kind = Kind::Identity;
        float a = 0, b = 0, c = 0;
        static LeafTransform Identity() { return {}; }
        static LeafTransform Scale(float belowZero, float aboveZero) { return {Kind::Scale, belowZero, aboveZero, 0}; }
        static LeafTransform Hyperbola(float base, float numerator, float shift) {
            return {Kind::Hyperbola, base, numerator, shift};
        }
        float Apply(float v) const {
            switch (kind) {
                case Kind::Identity: return v;
                case Kind::Scale: return v * (v < 0.0f ? a : b);
                case Kind::Hyperbola: return a - b / (v + c);
            }
            return v;
        }
    };

    class Spline {
    public:
        Spline(float constant) : constant(constant) {}
        Spline(SplineCoordinate coordinate, std::vector<float> locations, std::vector<Spline> values,
               std::vector<float> derivatives)
            : multipoint(true), coordinate(coordinate), locations(std::move(locations)), values(std::move(values)),
              derivatives(std::move(derivatives)) {}
        float Sample(const SplineInput& input) const;
        bool IsConstant() const { return !multipoint; }
        float Constant() const { return constant; }

    private:
        bool multipoint = false;
        float constant = 0.0f;
        SplineCoordinate coordinate = SplineCoordinate::Continents;
        std::vector<float> locations;
        std::vector<Spline> values;
        std::vector<float> derivatives;
    };

    float PeaksAndValleys(float weirdness);
    Spline OverworldOffset(LeafTransform leaves);
    Spline OverworldFactor(LeafTransform leaves);
    Spline OverworldJaggedness(LeafTransform leaves);
}
