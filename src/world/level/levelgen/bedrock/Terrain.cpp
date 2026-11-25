#include "world/level/levelgen/bedrock/Terrain.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace perlite::bedrock {

    namespace {
        constexpr float kMinCellY = -8.0f;
        constexpr float kCaveBoost = 128.0f;
        constexpr float kDepthPerBlock = -0.0078125f;
        constexpr float kSlideTopFloor = -10.0f;
        constexpr float kSlideTopOffset = 10.0f;
        constexpr float kSlideBottomOffset = -15.0f;
        constexpr float kSlideBottomTarget = 15.0f;
        constexpr float kElevationMax = 8.0f;

        float ClampUnit(float v) { return v < -1.0f ? -1.0f : std::fmin(1.0f, v); }
        float HalfShift(float n) { return (n + 1.0f) * 0.5f; }
        float FadeOut(float t, float start) {
            if (t < 0.0f)
                return start;
            if (t > 1.0f)
                return 0.0f;
            return t * -start + start;
        }
        // f32::max / f32::min: a NaN operand yields the other one.
        float Max(float a, float b) { return std::fmax(a, b); }
        float Min(float a, float b) { return std::fmin(a, b); }

        NormalNoise Make(const PositionalFactory& f, const char* name, int firstOctave, std::initializer_list<double> amps) {
            Xoroshiro r = f.HashOf(std::string("minecraft:") + name);
            return NormalNoise(r, firstOctave, std::span<const double>(amps.begin(), amps.size()));
        }
    }

    OverworldSettings OverworldSettings::For(Preset preset) {
        OverworldSettings s;
        const bool large = preset == Preset::LargeBiomes || preset == Preset::AmplifiedLargeBiomes;
        const bool amplified = preset == Preset::Amplified || preset == Preset::AmplifiedLargeBiomes;
        if (large)
            s.climate = ClimateScale{4.0f, 4.0f, 4.0f, 4.0f, 1.0f};
        if (amplified) {
            s.terrain.offset = LeafTransform::Scale(1.0f, 2.0f);
            s.terrain.factor = LeafTransform::Hyperbola(1.25f, 6.25f, 5.0f);
            s.terrain.jaggedness = LeafTransform::Scale(2.0f, 2.0f);
        }
        return s;
    }

    ClimatePoint ClimateColumn::Point(int y) const {
        const float depth = static_cast<float>(y) * kDepthPerBlock + 1.0f + offset;
        const float v[6] = {temperature, humidity, continents, erosion, depth, weirdness};
        ClimatePoint p{};
        for (int i = 0; i < 6; i++)
            p[static_cast<size_t>(i)] = SatCast<int64_t>(static_cast<double>(v[i]) * 10000.0);
        return p;
    }

    CaveNoises::CaveNoises(const PositionalFactory& f)
        : pillar(Make(f, "pillar", -7, {1.0, 1.0})), pillarRareness(Make(f, "pillar_rareness", -8, {1.0})),
          pillarThickness(Make(f, "pillar_thickness", -8, {1.0})), spaghetti2d(Make(f, "spaghetti_2d", -7, {1.0})),
          spaghetti2dElevation(Make(f, "spaghetti_2d_elevation", -8, {1.0})),
          spaghetti2dModulator(Make(f, "spaghetti_2d_modulator", -11, {1.0})),
          spaghetti2dThickness(Make(f, "spaghetti_2d_thickness", -11, {1.0})),
          spaghetti3d1(Make(f, "spaghetti_3d_1", -7, {1.0})), spaghetti3d2(Make(f, "spaghetti_3d_2", -7, {1.0})),
          spaghetti3dRarity(Make(f, "spaghetti_3d_rarity", -11, {1.0})),
          spaghetti3dThickness(Make(f, "spaghetti_3d_thickness", -8, {1.0})),
          spaghettiRoughness(Make(f, "spaghetti_roughness", -5, {1.0})),
          spaghettiRoughnessModulator(Make(f, "spaghetti_roughness_modulator", -8, {1.0})),
          caveEntrance(Make(f, "cave_entrance", -7, {0.4, 0.5, 1.0})), caveLayer(Make(f, "cave_layer", -8, {1.0})),
          caveCheese(Make(f, "cave_cheese", -8, {0.5, 1.0, 2.0, 1.0, 2.0, 1.0, 0.0, 2.0, 0.0})) {}

    float CaveNoises::Density(float x, float y, float z, float density) const {
        if (density < -150.0f)
            return density;
        const float roughness = Roughness(x, y, z);
        const float tunnels = Min(Spaghetti3d(x, y, z) + roughness, Entrance(x, y, z));
        if (density < 200.0f)
            return Min(tunnels * kCaveBoost * 5.0f, density);
        const float cheese = Cheese(x, y, z, density);
        const float caves = Min(cheese, Min(tunnels, roughness + Spaghetti2d(x, y, z)));
        return Max(caves, Pillar(x, y, z)) * kCaveBoost;
    }

    float CaveNoises::Roughness(float x, float y, float z) const {
        const float modulator = HalfShift(spaghettiRoughnessModulator.Value(x, y, z)) * 0.1f + 0.0f;
        return (0.4f - std::fabs(spaghettiRoughness.Value(x, y, z))) * modulator;
    }

    float CaveNoises::Spaghetti3d(float x, float y, float z) const {
        const float rarity = spaghetti3dRarity.Value(x + x, y, z + z);
        const float scale = rarity < -0.5f ? 0.75f : rarity < 0.0f ? 1.0f : rarity < 0.5f ? 1.5f : 2.0f;
        const float thickness = HalfShift(spaghetti3dThickness.Value(x, y, z)) * 0.023000002f + 0.065f;
        const float inverse = 1.0f / scale;
        const float first = spaghetti3d1.Value(x * inverse, y * inverse, z * inverse);
        const float second = spaghetti3d2.Value(x * inverse, y * inverse, z * inverse);
        return ClampUnit(Max(std::fabs(first * scale) - thickness, std::fabs(second * scale) - thickness));
    }

    float CaveNoises::Entrance(float x, float y, float z) const {
        const float noise = caveEntrance.Value(x * 0.75f, y * 0.5f, z * 0.75f) + 0.37f;
        const float height = (10.0f + y) / 40.0f;
        return noise + FadeOut(height, 0.3f);
    }

    float CaveNoises::Cheese(float x, float y, float z, float density) const {
        const float noise = ClampUnit(caveCheese.Value(x, 0.6666667f * y, z) + 0.27f);
        const float fade = (density + -200.0f) * 1.28f;
        const float layer = caveLayer.Value(x, 8.0f * y, z);
        return noise + FadeOut(fade, 0.5f) + layer * layer * 4.0f;
    }

    float CaveNoises::Spaghetti2d(float x, float y, float z) const {
        const float rarity = spaghetti2dModulator.Value(x + x, y, z + z);
        const float scale = rarity < -0.75f ? 0.5f : rarity < -0.5f ? 0.75f : rarity < 0.5f ? 1.0f : rarity < 0.75f ? 2.0f : 3.0f;
        const float thicknessNoise = spaghetti2dThickness.Value(x + x, y, z + z);
        const float inverse = 1.0f / scale;
        const float sheet = spaghetti2d.Value(x * inverse, y * inverse, z * inverse);
        const float elevationNoise = spaghetti2dElevation.Value(x, 0.0f, z);
        const float elevation = std::fabs(kMinCellY + -kElevationMax) > 1.1920929e-07f
                                    ? kMinCellY + (kElevationMax - kMinCellY) * HalfShift(elevationNoise)
                                    : kMinCellY;
        const float thickness = HalfShift(thicknessNoise) * 0.6999999f + 0.6f;
        const float ridge = std::fabs(scale * sheet) + -0.083f * thickness;
        const float offset = std::fabs(-0.125f * y + elevation) - thickness;
        return ClampUnit(Max(offset * offset * offset, ridge));
    }

    float CaveNoises::Pillar(float x, float y, float z) const {
        const float rareness = HalfShift(pillarRareness.Value(x, y, z)) * 2.0f + 0.0f;
        const float thickness = std::pow(HalfShift(pillarThickness.Value(x, y, z)) * 1.1f + 0.0f, 3.0f);
        const float value = (pillar.Value(x * 25.0f, y * 0.3f, z * 25.0f) * 2.0f - rareness) * thickness;
        return value > 0.03f ? value : std::numeric_limits<float>::lowest();
    }

    BedrockTerrain::BedrockTerrain(const PositionalFactory& f, const OverworldSettings& settings, DimensionDefinition definition)
        : temperature(Make(f, "temperature", -10, {1.5, 0.0, 1.0, 0.0, 0.0, 0.0})),
          vegetation(Make(f, "vegetation", -8, {1.0, 1.0, 0.0, 0.0, 0.0, 0.0})),
          continentalness(Make(f, "continentalness", -9, {1.0, 1.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0})),
          erosion(Make(f, "erosion", -9, {1.0, 1.0, 0.0, 1.0, 1.0})), ridge(Make(f, "ridge", -7, {1.0, 2.0, 1.0, 0.0, 0.0, 0.0})),
          offset(Make(f, "offset", -3, {1.0, 1.0, 1.0, 0.0})),
          jagged(Make(f, "jagged", -16, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1})), caves(f), blended(f),
          offsetSpline(OverworldOffset(settings.terrain.offset)), factorSpline(OverworldFactor(settings.terrain.factor)),
          jaggednessSpline(OverworldJaggedness(settings.terrain.jaggedness)),
          frequency{1.0f / settings.climate.temperature, 1.0f / settings.climate.humidity,
                    1.0f / settings.climate.continentalness, 1.0f / settings.climate.erosion,
                    1.0f / settings.climate.weirdness},
          globalOffset(settings.terrain.globalOffset), factorScaler(settings.terrain.factorScaler),
          seaLevel(settings.terrain.seaLevel), aquifers(settings.terrain.aquifers), oreVeins(settings.terrain.oreVeins),
          definition(definition) {}

    std::pair<float, float> BedrockTerrain::Shifted(float quartX, float quartZ) const {
        const float x = quartX + offset.Value(quartX, 0.0f, quartZ) * 4.0f;
        const float z = quartZ + offset.Value(quartZ, quartX, 0.0f) * 4.0f;
        return {x, z};
    }

    SplineInput BedrockTerrain::Input(float x, float z) const {
        const float continents = continentalness.Value(x * frequency.continentalness, 0.0f, z * frequency.continentalness);
        const float e = erosion.Value(x * frequency.erosion, 0.0f, z * frequency.erosion);
        const float weirdness = ridge.Value(x * frequency.weirdness, 0.0f, z * frequency.weirdness);
        const float ridges = (std::fabs(std::fabs(weirdness) + -0.6666667f) + -0.33333334f) * -3.0f;
        return SplineInput{continents, e, weirdness, ridges};
    }

    std::pair<float, float> BedrockTerrain::ErosionAndOffset(int quartX, int quartZ) const {
        const auto [sx, sz] = Shifted(static_cast<float>(quartX), static_cast<float>(quartZ));
        const SplineInput in = Input(sx, sz);
        return {in.erosion, offsetSpline.Sample(in)};
    }

    ClimateColumn BedrockTerrain::Column(int x, int z) const {
        const auto [sx, sz] = Shifted(static_cast<float>(x >> 2), static_cast<float>(z >> 2));
        const SplineInput in = Input(sx, sz);
        return ClimateColumn{temperature.Value(sx * frequency.temperature, 0.0f, sz * frequency.temperature),
                             vegetation.Value(sx * frequency.humidity, 0.0f, sz * frequency.humidity),
                             in.continents,
                             in.erosion,
                             in.weirdness,
                             OffsetOf(in)};
    }

    int BedrockTerrain::PreliminarySurface(int quartX, int quartZ) const {
        const auto [sx, sz] = Shifted(static_cast<float>(quartX), static_cast<float>(quartZ));
        const SplineInput in = Input(sx, sz);
        const float off = OffsetOf(in);
        const float factor = FactorOf(in);
        int y = definition.Top() - 72;
        const int count = (definition.height - 72) / kCellHeight + 1;
        for (int steps = 0; steps < count; steps++) {
            const int counter = count - steps;
            float density = (static_cast<float>(y) * kDepthPerBlock + 1.0f + off) * factor;
            density *= density > 0.0f ? 4.0f : 1.0f;
            density += -90.0f;
            const float top = static_cast<float>(steps + 1) * 0.5f;
            if (top < 0.0f)
                density = kSlideTopFloor;
            else if (top <= 1.0f)
                density = (density + kSlideTopOffset) * top + kSlideTopFloor;
            const float bottom = static_cast<float>((counter - 1) & 0xff) / 3.0f;
            if (bottom <= 1.0f)
                density = (density + kSlideBottomOffset) * bottom + kSlideBottomTarget;
            if (density > 50.0f)
                return y;
            y -= kCellHeight;
        }
        return kSurfaceNotFound;
    }

    BedrockTerrain::Shape BedrockTerrain::ColumnShape(int quartX, int quartZ) const {
        const auto [sx, sz] = Shifted(static_cast<float>(quartX), static_cast<float>(quartZ));
        const SplineInput in = Input(sx, sz);
        const float jv = jaggednessSpline.Sample(in);
        float jaggedness = 0.0f;
        if (jv > 0.0f) {
            const float noise =
                jagged.Value(static_cast<float>(quartX) * 4.0f * 1500.0f, 0.0f, static_cast<float>(quartZ) * 4.0f * 1500.0f);
            jaggedness = noise > 0.0f ? jv * noise : (jv * 0.5f) * noise;
        }
        return Shape{OffsetOf(in), FactorOf(in), jaggedness};
    }

    void BedrockTerrain::DensityCorners(int chunkX, int chunkZ, TerrainBuffers& buffers, std::vector<float>& out) const {
        const int minY = definition.minY;
        const int levels = NoiseLevels();
        const int divY = minY >= 0 ? minY / kCellHeight : -((-minY + kCellHeight - 1) / kCellHeight);
        const std::array<float, 3> position{static_cast<float>(chunkX * kCellWidth), static_cast<float>(divY),
                                            static_cast<float>(chunkZ * kCellWidth)};
        blended.Fill(buffers, position, levels);
        out.clear();
        out.reserve(static_cast<size_t>(kGridColumns * kGridColumns * levels));
        size_t index = 0;
        for (int cx = 0; cx < kGridColumns; cx++)
            for (int cz = 0; cz < kGridColumns; cz++) {
                const int quartX = chunkX * kCellWidth + cx;
                const int quartZ = chunkZ * kCellWidth + cz;
                const Shape shape = ColumnShape(quartX, quartZ);
                for (int cy = 0; cy < levels; cy++) {
                    const int y = minY + kCellHeight * cy;
                    const float minimum = buffers.minLimit[index] * 0.001953125f;
                    const float blend = (buffers.main[index] / 10.0f + 1.0f) * 0.5f;
                    float blendedValue;
                    if (0.0f > blend) {
                        blendedValue = minimum;
                    } else {
                        const float maximum = buffers.maxLimit[index] * 0.001953125f;
                        blendedValue = blend > 1.0f ? maximum : minimum + blend * (maximum - minimum);
                    }
                    float base = (static_cast<float>(y) * kDepthPerBlock + 1.0f + shape.offset + shape.jaggedness) * shape.factor;
                    base *= base > 0.0f ? 4.0f : 1.0f;
                    float density = base + blendedValue;
                    density = caves.Density(static_cast<float>(quartX * 4), static_cast<float>(y), static_cast<float>(quartZ * 4), density);
                    const float top = static_cast<float>(CellsY() - 8 - cy) * 0.5f;
                    density = 0.0f > top ? kSlideTopFloor : top > 1.0f ? density : (density + kSlideTopOffset) * top + kSlideTopFloor;
                    const float bottom = static_cast<float>(cy) / 3.0f;
                    density = bottom > 1.0f ? density : (density + kSlideBottomOffset) * bottom + kSlideBottomTarget;
                    out.push_back(density);
                    index++;
                }
            }
    }
}
