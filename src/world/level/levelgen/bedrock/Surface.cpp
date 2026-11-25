#include "world/level/levelgen/bedrock/Surface.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/bedrock/Biomes.hpp"

#include <cmath>
#include <string>

namespace perlite::bedrock {

    // ---------------------------------------------------------------- simplex

    namespace {
        constexpr float kGradients[16][3] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
            {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1},
        };
        int FastFloor(float v) { return SatCast(v) - (v <= 0.0f ? 1 : 0); }
        float Corner2(int g, float x, float y) {
            const float f = 0.5f - x * x - y * y;
            if (f < 0.0f)
                return 0.0f;
            return f * f * f * f * (kGradients[g][0] * x + kGradients[g][1] * y);
        }
        float Corner3(int g, float x, float y, float z) {
            const float f = ((0.6f - x * x) - y * y) - z * z;
            if (f < 0.0f)
                return 0.0f;
            return ((kGradients[g][2] * z + kGradients[g][0] * x) + kGradients[g][1] * y) * f * f * f * f;
        }
        constexpr float kSkew2 = 0.36602542f, kUnskew2 = 0.21132487f, kSkew3 = 0.33333334f, kUnskew3 = 0.16666667f;
    }

    template <class R>
    SurfaceSimplex::SurfaceSimplex(R& random, bool newOrder) {
        const float a = random.NextFloat(), b = random.NextFloat(), c = random.NextFloat();
        origin = newOrder ? std::array<float, 3>{a * 256.0f, b * 256.0f, c * 256.0f} : std::array<float, 3>{c * 256.0f, b * 256.0f, a * 256.0f};
        perm.fill(0);
        for (int i = 0; i < 256; i++)
            perm[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        for (int i = 0; i < 256; i++) {
            const int j = random.NextInt(256 - i) + i;
            std::swap(perm[static_cast<size_t>(i)], perm[static_cast<size_t>(j)]);
            perm[static_cast<size_t>(i + 256)] = perm[static_cast<size_t>(i)];
        }
    }
    template SurfaceSimplex::SurfaceSimplex(MersenneTwister&, bool);
    template SurfaceSimplex::SurfaceSimplex(Xoroshiro&, bool);

    float SurfaceSimplex::Corners2d(float x, float y) const {
        const float skew = (x + y) * kSkew2;
        const int i = FastFloor(x + skew), j = FastFloor(y + skew);
        const float unskew = static_cast<float>(i + j) * kUnskew2;
        const float x0 = x - (static_cast<float>(i) - unskew), y0 = y - (static_cast<float>(j) - unskew);
        const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        const float x1 = x0 - static_cast<float>(i1) + kUnskew2, y1 = y0 - static_cast<float>(j1) + kUnskew2;
        const float x2 = x0 - 1.0f + (kUnskew2 + kUnskew2), y2 = y0 - 1.0f + (kUnskew2 + kUnskew2);
        const int ii = i & 255, jj = j & 255;
        const int g0 = P(ii + P(jj)) % 12, g1 = P(ii + i1 + P(jj + j1)) % 12, g2 = P(ii + 1 + P(jj + 1)) % 12;
        return (Corner2(g0, x0, y0) + Corner2(g1, x1, y1)) + Corner2(g2, x2, y2);
    }

    float SurfaceSimplex::Sample3d(float x, float y, float z) const {
        const float skew = (x + y + z) * kSkew3;
        const int i = FastFloor(skew + x), j = FastFloor(y + skew), k = FastFloor(skew + z);
        const float unskew = static_cast<float>(i + j + k) * kUnskew3;
        const float x0 = x - (static_cast<float>(i) - unskew), y0 = y - (static_cast<float>(j) - unskew), z0 = z - (static_cast<float>(k) - unskew);
        int i1, j1, k1, i2, j2, k2;
        if (y0 <= x0) {
            if (z0 <= y0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (z0 <= x0) { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        const float x1 = (x0 - static_cast<float>(i1)) + kUnskew3, y1 = (y0 - static_cast<float>(j1)) + kUnskew3, z1 = (z0 - static_cast<float>(k1)) + kUnskew3;
        const float x2 = (x0 - static_cast<float>(i2)) + 0.33333334f, y2 = (y0 - static_cast<float>(j2)) + 0.33333334f, z2 = (z0 - static_cast<float>(k2)) + 0.33333334f;
        const float x3 = x0 + -1.0f + 0.5f, y3 = y0 + -1.0f + 0.5f, z3 = z0 + -1.0f + 0.5f;
        const int ii = i & 255, jj = j & 255, kk = k & 255;
        auto g = [&](int a, int b, int c) { return P(ii + a + P(jj + b + P(kk + c))) % 12; };
        const float n0 = Corner3(g(0, 0, 0), x0, y0, z0);
        const float n1 = Corner3(g(i1, j1, k1), x1, y1, z1);
        const float n2 = Corner3(g(i2, j2, k2), x2, y2, z2);
        const float n3 = Corner3(g(1, 1, 1), x3, y3, z3);
        return (((n0 + n1) + n2) + n3) * 32.0f;
    }

    void SurfaceSimplex::AddRegion(std::array<float, 256>& buffer, float originX, float originZ, float step, float amplitude) const {
        for (int zi = 0; zi < 16; zi++) {
            const float y = (static_cast<float>(zi) + originZ) * step + origin[1];
            for (int xi = 0; xi < 16; xi++) {
                const float x = (static_cast<float>(xi) + originX) * step + origin[0];
                buffer[static_cast<size_t>(zi * 16 + xi)] += Corners2d(x, y) * 70.0f * amplitude;
            }
        }
    }

    float PerlinSimplex::Sum2d(float x, float z) const {
        float sum = 0.0f, frequency = 1.0f;
        for (const auto& l : levels) {
            sum += l.Sample2d(frequency * x, frequency * z) / frequency;
            frequency *= 0.5f;
        }
        return sum;
    }

    float PerlinSimplex::Sum3d(float x, float y, float z) const {
        float sum = 0.0f, frequency = 1.0f;
        for (const auto& l : levels) {
            sum += l.Sample3d(x * frequency, y * frequency, z * frequency) / frequency;
            frequency *= 0.5f;
        }
        return sum;
    }

    std::array<float, 256> PerlinSimplex::Region(int chunkX, int chunkZ) const {
        std::array<float, 256> buffer{};
        float frequency = 1.0f;
        for (const auto& l : levels) {
            l.AddRegion(buffer, static_cast<float>(chunkX * 16), static_cast<float>(chunkZ * 16), frequency * 0.125f, 0.55f / frequency);
            frequency *= 0.5f;
        }
        return buffer;
    }

    // ---------------------------------------------------------------- data

    namespace {
        uint16_t Block(const std::string& name) { return BlockCatalog::Instance().DefaultBlock(name); }
    }

    SurfacePalette::SurfacePalette() {
        bedrock = Block("bedrock");
        stone = Block("stone");
        water = Block("water");
        lava = Block("lava");
        sand = Block("sand");
        redSand = Block("red_sand");
        suspiciousSand = Block("suspicious_sand");
        sandstone = Block("sandstone");
        redSandstone = Block("red_sandstone");
        gravel = Block("gravel");
        ice = Block("ice");
        packedIce = Block("packed_ice");
        blueIce = Block("blue_ice");
        snowBlock = Block("snow");
        coarseDirt = Block("coarse_dirt");
        grassBlock = Block("grass_block");
        netherrack = Block("netherrack");
        hardenedClay = Block("hardened_clay");
        static const char* colors[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                         "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
        for (int i = 0; i < 16; i++)
            terracotta[static_cast<size_t>(i)] = Block(std::string(colors[i]) + "_terracotta");
        static const char* found[7] = {"stone", "granite", "polished_granite", "diorite", "polished_diorite", "andesite", "polished_andesite"};
        for (int i = 0; i < 7; i++)
            foundation[static_cast<size_t>(i)] = Block(found[i]);
    }

    struct Materials {
        uint16_t top, mid, seaFloor, foundation, sea;
    };

    struct Adjustment {
        float scale, low, high;
        std::optional<uint16_t> top, mid, seaFloor, foundation, sea;
        void Apply(Materials& m) const {
            m.top = top.value_or(m.top);
            m.mid = mid.value_or(m.mid);
            m.seaFloor = seaFloor.value_or(m.seaFloor);
            m.foundation = foundation.value_or(m.foundation);
            m.sea = sea.value_or(m.sea);
        }
    };

    struct Steep {
        uint16_t block;
        bool north, south, east, west;
    };

    struct Specifier {
        float low, high;
        uint16_t block;
    };

    enum class BuilderKind { Overworld, Swamp, FrozenOcean, Mesa, NoiseGradient };

    struct BiomeSurface {
        float temperature;
        bool partiallyFrozen;
        BuilderKind kind;
        int puddleDepth = 0;
        bool brycePillars = false, hasForest = false;
        uint16_t hardClay = 0;
        std::optional<NormalNoise> gradientNoise;
        std::vector<Specifier> specifiers;
        std::vector<uint16_t> nonReplaceable;
        Materials materials;
        int seaFloorDepth;
        std::optional<Steep> steep;
        std::vector<Adjustment> adjustments;
    };

    namespace {
        std::optional<std::string> MaterialName(const json::Value& v) {
            std::string name = v.IsString() ? v.text : v["name"].String();
            if (name.empty())
                return std::nullopt;
            if ((name == "minecraft:dirt" || name == "dirt") && v["states"]["dirt_type"].String() == "coarse")
                return std::string("minecraft:coarse_dirt");
            return name;
        }
        uint16_t BlockOr(const json::Value& v, uint16_t fallback) {
            const auto n = MaterialName(v);
            return n ? Block(*n) : fallback;
        }
        std::optional<uint16_t> OptionalBlock(const json::Value& v) {
            const auto n = MaterialName(v);
            return n ? std::optional<uint16_t>(Block(*n)) : std::nullopt;
        }

        std::unique_ptr<BiomeSurface> ParseBiome(const json::Value& components, const PositionalFactory& factory, const SurfacePalette& p) {
            const json::Value& builder = components["minecraft:surface_builder"]["builder"];
            if (!builder.IsObject())
                return nullptr;
            auto s = std::make_unique<BiomeSurface>();
            const std::string type = builder["type"].String();
            if (type == "minecraft:overworld") {
                s->kind = BuilderKind::Overworld;
            } else if (type == "minecraft:swamp") {
                s->kind = BuilderKind::Swamp;
                s->puddleDepth = static_cast<int>(builder["max_puddle_depth_below_sea_level"].Int(0));
            } else if (type == "minecraft:frozen_ocean") {
                s->kind = BuilderKind::FrozenOcean;
            } else if (type == "minecraft:mesa") {
                s->kind = BuilderKind::Mesa;
                s->brycePillars = builder["bryce_pillars"].Bool(false);
                s->hasForest = builder["has_forest"].Bool(false);
                s->hardClay = BlockOr(builder["hard_clay_material"], p.hardenedClay);
            } else if (type == "minecraft:noise_gradient") {
                s->kind = BuilderKind::NoiseGradient;
                const json::Value& d = builder["noise_descriptor"];
                std::vector<double> amplitudes;
                for (const auto& a : d["amplitudes"].items)
                    amplitudes.push_back(a.Number());
                Xoroshiro r = factory.HashOf(d["name"].String());
                s->gradientNoise.emplace(r, static_cast<int>(d["first_octave"].Int()), amplitudes);
                for (const auto& spec : builder["noise_block_specifiers"].items)
                    s->specifiers.push_back(Specifier{static_cast<float>(spec["range"]["min"].Number()),
                                                      static_cast<float>(spec["range"]["max"].Number()), BlockOr(spec["block"], 0)});
                for (const auto& b : builder["non_replaceable_blocks"].items)
                    s->nonReplaceable.push_back(BlockOr(b, 0));
            } else {
                return nullptr;
            }
            s->temperature = static_cast<float>(components["minecraft:climate"]["temperature"].Number());
            s->partiallyFrozen = !components["minecraft:partially_frozen"].IsNull();
            s->materials = Materials{BlockOr(builder["top_material"], 0), BlockOr(builder["mid_material"], 0),
                                     BlockOr(builder["sea_floor_material"], 0), BlockOr(builder["foundation_material"], p.stone),
                                     BlockOr(builder["sea_material"], p.water)};
            s->seaFloorDepth = static_cast<int>(builder["sea_floor_depth"].Int(0));
            const json::Value& steep = components["minecraft:mountain_parameters"]["steep_material_adjustment"];
            if (steep.IsObject())
                s->steep = Steep{BlockOr(steep["material"], 0), steep["north_slopes"].Bool(false), steep["south_slopes"].Bool(false),
                                 steep["east_slopes"].Bool(false), steep["west_slopes"].Bool(false)};
            for (const auto& entry : components["minecraft:surface_material_adjustments"]["adjustments"].items) {
                if (!entry["height_range"].IsNull())
                    continue;
                const json::Value& m = entry["materials"];
                s->adjustments.push_back(Adjustment{static_cast<float>(entry["noise_frequency_scale"].Number()),
                                                    static_cast<float>(entry["noise_range"][0].Number()),
                                                    static_cast<float>(entry["noise_range"][1].Number()), OptionalBlock(m["top_material"]),
                                                    OptionalBlock(m["mid_material"]), OptionalBlock(m["sea_floor_material"]),
                                                    OptionalBlock(m["foundation_material"]), OptionalBlock(m["sea_material"])});
            }
            return s;
        }

        std::array<uint8_t, 192> GenerateBands(uint32_t seed) {
            MersenneTwister random(seed);
            std::array<uint8_t, 192> bands;
            bands.fill(16);
            auto stripe = [&](uint8_t color, int extra) {
                const int count = random.NextInt(10);
                for (int n = 0; n < std::max(count + 5, 0) + 1; n++) {
                    const int length = random.NextInt(3) + extra;
                    const int start = random.NextInt(192);
                    if (length < 0)
                        continue;
                    const int last = std::min(length, 191 - start);
                    for (int o = 0; o <= last; o++)
                        bands[static_cast<size_t>(start + o)] = color;
                }
            };
            int index = 0;
            for (;;) {
                const int gap = random.NextInt(5);
                const int position = gap + index + 1;
                if (position >= 192)
                    break;
                bands[static_cast<size_t>(position)] = 1;
                index = gap + index + 2;
                if (position == 191)
                    break;
            }
            stripe(4, 0);
            stripe(12, 1);
            stripe(14, 0);
            const int limit = random.NextInt(7);
            size_t position = 0;
            int iteration = 0;
            for (;;) {
                bands[position] = 0;
                if (position >= 2) {
                    if (random.NextBoolean())
                        bands[position - 1] = 8;
                    if (position < 191 && random.NextBoolean())
                        bands[position + 1] = 8;
                } else if (random.NextBoolean()) {
                    bands[position + 1] = 8;
                }
                const size_t step = static_cast<size_t>(random.NextInt(16));
                if (limit + 8 <= iteration)
                    break;
                iteration++;
                position += step + 4;
                if (position >= 192)
                    break;
            }
            return bands;
        }

        NormalNoise MakeNoise(const PositionalFactory& f, const char* name, int octave, int count) {
            Xoroshiro r = f.HashOf(name);
            std::vector<double> a(static_cast<size_t>(count), 1.0);
            return NormalNoise(r, octave, a);
        }

        PositionalFactory Level32Factory(int64_t seed) {
            Xoroshiro x(static_cast<int64_t>(LevelSeed32(seed)));
            return x.ForkPositional();
        }
    }

    struct SurfacePass::Temperature {
        PerlinSimplex frozenMajor, frozenMinor, height;
        static PerlinSimplex Seeded(uint32_t seed, int levels) {
            MersenneTwister r(seed);
            return PerlinSimplex(r, levels, false);
        }
        Temperature() : frozenMajor(Seeded(3456, 3)), frozenMinor(Seeded(2345, 1)), height(Seeded(1234, 1)) {}
        float At(float base, bool partiallyFrozen, int x, int y, int z, int seaLevel) const {
            const float fx = static_cast<float>(x), fz = static_cast<float>(z);
            float t = base;
            if (partiallyFrozen) {
                const float major = frozenMajor.Sum2d(fx * 0.05f, fz * 0.05f);
                const float minor = frozenMinor.Sum2d(fx * 0.2f, fz * 0.2f);
                if (major + minor < 0.3f && frozenMinor.Sum2d(fx * 0.09f, fz * 0.09f) < 0.8f)
                    t = 0.2f;
            }
            const int coolingStart = seaLevel + 17;
            if (y > coolingStart) {
                const float noise = height.Sum2d(fx * 0.125f, fz * 0.125f);
                const float cached = static_cast<float>(static_cast<int8_t>(SatCast(noise * 64.0f))) * 0.125f;
                t += (static_cast<float>(y) + cached - static_cast<float>(coolingStart)) * -0.05f / 40.0f;
            }
            return t;
        }
    };

    SurfacePass::SurfacePass(int64_t seed, const PositionalFactory& factory, const Pack& pack)
        : depth([&] {
              Xoroshiro r = factory.HashOf("minecraft:surface");
              return PerlinSimplex(r, 4, true);
          }()),
          adjustment([&] {
              MersenneTwister r = MaterialAdjustmentRandom(seed);
              return PerlinSimplex(r, 3, false);
          }()),
          swamp([&] {
              MersenneTwister r(static_cast<uint32_t>(LevelSeed32(seed)));
              return PerlinSimplex(r, 2, false);
          }()),
          temperature(std::make_unique<Temperature>()),
          icebergSurface(MakeNoise(Level32Factory(seed), "minecraft:iceberg_surface", -6, 3)),
          icebergPillar(MakeNoise(Level32Factory(seed), "minecraft:iceberg_pillar", -6, 4)),
          icebergRoof(MakeNoise(Level32Factory(seed), "minecraft:iceberg_pillar_roof", -3, 1)), icebergFactory(Level32Factory(seed)),
          clayBandsOffset(MakeNoise(Level32Factory(seed), "minecraft:clay_bands_offset", -8, 1)),
          badlandsSurface(MakeNoise(Level32Factory(seed), "minecraft:badlands_surface", -6, 3)),
          badlandsPillar(MakeNoise(Level32Factory(seed), "minecraft:badlands_pillar", -2, 4)),
          badlandsRoof(MakeNoise(Level32Factory(seed), "minecraft:badlands_pillar_roof", -8, 1)),
          bands(GenerateBands(static_cast<uint32_t>(LevelSeed32(seed)))) {
        for (int i = 0; i < kBiomeTableSize; i++) {
            if (!kBiomeTable[i].generated)
                continue;
            const json::Value* c = pack.BiomeComponents(kBiomeTable[i].pack);
            if (!c)
                continue;
            if (auto s = ParseBiome(*c, factory, palette))
                biomes[kBiomeTable[i].id] = std::move(s);
        }
    }

    SurfacePass::~SurfacePass() = default;

    std::optional<std::pair<float, bool>> SurfacePass::Climate(int biomeId) const {
        const auto it = biomes.find(biomeId);
        if (it == biomes.end())
            return std::nullopt;
        return std::pair<float, bool>{it->second->temperature, it->second->partiallyFrozen};
    }

    float SurfacePass::TemperatureAt(int biomeId, int x, int y, int z, int seaLevel) const {
        const auto c = Climate(biomeId);
        return c ? temperature->At(c->first, c->second, x, y, z, seaLevel) : 0.8f;
    }

    bool SurfacePass::IsSteep(const ProtoChunk& chunk, const std::array<int, 256>& tops, int lx, int lz, const Steep& steep) const {
        const int minY = chunk.minY;
        auto heightAt = [&](int x, int z) {
            int y = std::min(tops[static_cast<size_t>(z * 16 + x)] - minY, chunk.height - 1);
            while (y > 0) {
                if (chunk.Get(x, minY + y, z) != palette.air)
                    return y;
                y--;
            }
            return 0;
        };
        if (steep.north || steep.south) {
            const int d = heightAt(lx, std::max(lz - 1, 0)) - heightAt(lx, std::min(lz, 14) + 1);
            if ((steep.south && d > 3) || (steep.north && d < -3))
                return true;
        }
        if (steep.west || steep.east) {
            const int d = heightAt(std::max(lx, 1) - 1, lz) - heightAt(std::min(lx, 14) + 1, lz);
            if ((steep.east && d > 3) || (steep.west && d < -3))
                return true;
        }
        return false;
    }

    namespace {
        constexpr int kNoWater = static_cast<int>(0x800003e8u);

        struct Column {
            ProtoChunk& chunk;
            int x, z;
            uint16_t Get(int y) const { return chunk.Get(x, y, z); }
            void Set(int y, uint16_t b) { chunk.Set(x, y, z, b); }
            uint16_t GetLocal(int y) const { return Get(y + chunk.minY); }
            void SetLocal(int y, uint16_t b) { Set(y + chunk.minY, b); }
            int MaxY() const { return chunk.minY + chunk.height - 1; }
        };

        struct DefaultColumn {
            int startY, lowerBound, runDepth;
            Materials materials;
            std::optional<uint16_t> steep;
            int seaFloorDepth;
            float temperature;
        };

        void SolidifyAbove(const SurfacePalette& p, Column& c, int y, uint16_t foundation) {
            const int above = y + 1;
            if (above > c.MaxY())
                return;
            const uint16_t b = c.Get(above);
            if (b == p.sand)
                c.Set(above, p.sandstone);
            else if (b == p.redSand)
                c.Set(above, p.redSandstone);
            else if (b == p.gravel)
                c.Set(above, foundation);
        }

        void BuildDefault(const SurfacePalette& p, Column& c, const DefaultColumn& spec, MersenneTwister& random) {
            const Materials& m = spec.materials;
            const uint16_t foundation = m.foundation;
            uint16_t topBase = m.top, midBase = m.mid, topVar = topBase, midVar = midBase;
            int run = -1;
            int water = kNoWater;
            for (int y = spec.startY; y >= spec.lowerBound + 1; y--) {
                const uint16_t block = c.Get(y);
                if (block == p.bedrock)
                    continue;
                if (run >= 0 && foundation != p.netherrack && (block == p.air || p.IsLiquid(block)))
                    SolidifyAbove(p, c, y, foundation);
                if (block == p.air) {
                    run = -1;
                    water = kNoWater;
                    continue;
                }
                if (spec.steep) {
                    topBase = midBase = topVar = midVar = *spec.steep;
                }
                const int waterLevel = water;
                if (!p.IsFoundation(block)) {
                    uint16_t current = block;
                    if (block == p.water) {
                        current = m.sea;
                        c.Set(y, current);
                    }
                    if (p.IsLiquid(current) || current == p.packedIce || current == p.blueIce)
                        water = std::max(water, y + 1);
                    continue;
                }
                if (run == -1) {
                    uint16_t topNext, midNext;
                    if (spec.runDepth > 0) {
                        const bool inRange = !(y < waterLevel - 4 || y > waterLevel + 1);
                        topNext = inRange ? topBase : topVar;
                        midNext = inRange ? midBase : midVar;
                    } else {
                        topNext = p.air;
                        midNext = foundation;
                    }
                    if (y < waterLevel && topNext == p.air)
                        topNext = m.sea == p.water && spec.temperature < 0.15f ? p.ice : m.sea;
                    run = spec.runDepth;
                    if (y >= waterLevel - 1) {
                        c.Set(y, topNext);
                        midVar = midNext;
                        topVar = topNext;
                    } else if (y < waterLevel - (spec.seaFloorDepth + spec.runDepth) || y == spec.lowerBound + 1) {
                        c.Set(y, m.seaFloor);
                        midVar = foundation;
                        topVar = p.air;
                    } else {
                        c.Set(y, midNext);
                        midVar = midNext;
                        topVar = topNext;
                    }
                } else if (run > 0) {
                    c.Set(y, midVar);
                    run--;
                    if (run == 0 && p.IsSand(midVar)) {
                        run = random.NextInt(4) + std::max(y - waterLevel, 0);
                        midVar = p.sandstone;
                    }
                } else {
                    c.Set(y, foundation);
                }
            }
        }

        int RunDepth(MersenneTwister& random, float depth) { return SatCast(random.NextFloat() * 0.25f + (depth / 3.0f + 3.0f)); }
    }

    std::array<int, 256> SurfacePass::BuildColumns(const BedrockTerrain& terrain, ProtoChunk& chunk, const BiomeVolume& volume) const {
        const int minY = chunk.minY, maxY = chunk.minY + chunk.height - 1;
        const int seaLevel = terrain.SeaLevel();
        const std::array<float, 256> depthRegion = depth.Region(chunk.x, chunk.z);
        MersenneTwister random = ChunkRandom(chunk.x, chunk.z);
        std::array<int, 256> tops{};
        for (int i = 0; i < 256; i++)
            tops[static_cast<size_t>(i)] = chunk.tops[static_cast<size_t>(((i / 16) << 4) | (i % 16))];
        std::array<int, 16> preliminary{};
        for (int i = 0; i < 16; i++)
            preliminary[static_cast<size_t>(i)] = terrain.PreliminarySurface(chunk.x * 4 + i / 4, chunk.z * 4 + i % 4);
        std::array<int, 256> lowerBounds;
        lowerBounds.fill(minY);
        for (int lx = 0; lx < 16; lx++)
            for (int lz = 0; lz < 16; lz++) {
                const int layers = random.NextInt(4) + 2;
                for (int o = 0; o < layers; o++)
                    chunk.Set(lx, minY + o, lz, palette.bedrock);
            }
        for (int lx = 0; lx < 16; lx++)
            for (int lz = 0; lz < 16; lz++) {
                const size_t index = static_cast<size_t>(lz * 16 + lx);
                const int top = tops[index];
                const auto it = biomes.find(volume.IdAt(lx, top, lz));
                if (it == biomes.end())
                    continue;
                const BiomeSurface& surface = *it->second;
                const int x = chunk.MinX() + lx, z = chunk.MinZ() + lz;
                const float fx = static_cast<float>(x), fz = static_cast<float>(z);
                const int prelim = preliminary[static_cast<size_t>((lx >> 2) * 4 + (lz >> 2))];
                const int lowerBound = prelim == kSurfaceNotFound ? minY : std::min(std::max(prelim - 8, minY), maxY);
                lowerBounds[index] = lowerBound;
                Materials materials = surface.materials;
                for (const Adjustment& a : surface.adjustments) {
                    const float noise = adjustment.Sum3d(fx * a.scale, 0.0f * a.scale, fz * a.scale) * (1.0f / 7.0f);
                    if (a.low <= noise && noise < a.high)
                        a.Apply(materials);
                }
                std::optional<uint16_t> steep;
                if (surface.steep && IsSteep(chunk, tops, lx, lz, *surface.steep))
                    steep = surface.steep->block;
                const int startY = std::min(minY + chunk.height - 1, top);
                const float temp = temperature->At(surface.temperature, surface.partiallyFrozen, x, 0, z, seaLevel);
                const float depthValue = depthRegion[static_cast<size_t>(lz * 16 + lx)];
                Column column{chunk, lx, lz};
                auto spec = [&](int runDepth) { return DefaultColumn{startY, lowerBound, runDepth, materials, steep, surface.seaFloorDepth, temp}; };
                switch (surface.kind) {
                    case BuilderKind::Overworld: {
                        const DefaultColumn s = spec(RunDepth(random, depthValue));
                        BuildDefault(palette, column, s, random);
                        break;
                    }
                    case BuilderKind::Swamp: {
                        if (!(swamp.Sum2d(fx * 0.25f, fz * 0.25f) <= 0.0f || seaLevel < top)) {
                            for (int y = top - 1; y >= seaLevel - surface.puddleDepth; y--)
                                if (column.Get(y) != palette.water) {
                                    column.Set(y, palette.water);
                                    break;
                                }
                        }
                        const DefaultColumn s = spec(RunDepth(random, depthValue));
                        BuildDefault(palette, column, s, random);
                        break;
                    }
                    case BuilderKind::FrozenOcean: {
                        // Iceberg column.
                        const float sv = std::fabs(icebergSurface.Value(fx, 0.0f, fz) * 8.25f);
                        const float pv = icebergPillar.Value(fx * 1.28f, 0.0f, fz * 1.28f) * 15.0f;
                        const float d = sv < pv ? sv : pv;
                        if (d > 1.8f) {
                            const int sea = seaLevel - minY;
                            const float roof = std::fabs(icebergRoof.Value(fx * 1.17f, 0.0f, fz * 1.17f) * 1.5f) * 40.0f;
                            float height = d * d * 1.2f;
                            const float ceiling = std::ceil(roof) + 14.0f;
                            if (ceiling <= height)
                                height = ceiling;
                            if (temp > 0.1f)
                                height += -2.0f;
                            const bool solid = 2.0f < height;
                            const float underwater = solid ? (static_cast<float>(sea) - height) - 7.0f : 0.0f;
                            const float topF = solid ? static_cast<float>(sea) + height : 0.0f;
                            Xoroshiro r = icebergFactory.AtWide(x, 0, z);
                            const int maxSnow = r.NextInt(4) + 2;
                            const int snowFloor = r.NextInt(10) + sea + 18;
                            const float hmLocal = static_cast<float>(top - minY);
                            const float start = topF + 1.0f;
                            const int first = SatCast(hmLocal <= start ? start : hmLocal);
                            const int lower = lowerBound - minY;
                            const int topLimit = SatCast(topF), underwaterLimit = SatCast(underwater);
                            int snowCount = 0;
                            for (int y = first; y >= lower; y--) {
                                if (y >= chunk.height)
                                    continue;
                                const uint16_t b = column.GetLocal(y);
                                bool placed = false;
                                if (b == palette.air && y < topLimit)
                                    placed = r.NextFloat() > 0.01f;
                                else if (underwater != 0.0f && b == palette.water && underwaterLimit < y && y < sea)
                                    placed = r.NextFloat() > 0.15f;
                                if (!placed)
                                    continue;
                                if (maxSnow < snowCount || y <= snowFloor) {
                                    column.SetLocal(y, palette.packedIce);
                                } else {
                                    column.SetLocal(y, palette.snowBlock);
                                    snowCount++;
                                }
                            }
                            const DefaultColumn s = spec(RunDepth(random, depthValue));
                            BuildDefault(palette, column, s, random);
                        }
                        break;
                    }
                    case BuilderKind::Mesa: {
                        float pillarTop = 0.0f;
                        if (surface.brycePillars) {
                            const float sv = std::fabs(badlandsSurface.Value(fx, 0.0f, fz) * 8.25f);
                            const float pv = badlandsPillar.Value(fx * 0.2f, 0.0f, fz * 0.2f) * 15.0f;
                            const float d = pv <= sv ? pv : sv;
                            if (0.0f < d) {
                                const float roof = std::fabs(badlandsRoof.Value(fx * 0.75f, 0.0f, fz * 0.75f) * 1.5f) * 50.0f;
                                float t = d * d * 2.5f;
                                const float ceiling = std::ceil(roof) + 24.0f;
                                if (ceiling <= t)
                                    t = ceiling;
                                pillarTop = t + 64.0f;
                            }
                        }
                        const float depthThird = depthValue / 3.0f;
                        const float randomValue = random.NextFloat();
                        const float cosine = std::cos(3.14159274101257324f * depthThird);
                        const int height = chunk.height;
                        const int lower = lowerBound - minY;
                        const int sea = seaLevel - minY;
                        bool liquidTop = false;
                        for (int y = height; y >= lower + 1; y--) {
                            const uint16_t b = column.GetLocal(y - 1);
                            if (palette.IsLiquid(b)) {
                                liquidTop = true;
                                break;
                            }
                            if (b != palette.air)
                                break;
                        }
                        if (liquidTop)
                            break;
                        const int runDepth = SatCast(randomValue * 0.25f + (depthThird + 3.0f));
                        const int bandOffset = SatCast(std::round(clayBandsOffset.Value(fx, 0.0f, fz) * 4.0f));
                        auto bandColor = [&](int y) -> uint8_t {
                            int i = (y + 192 + bandOffset) % 192;
                            if (i < 0)
                                i += 192;
                            return bands[static_cast<size_t>(i)];
                        };
                        auto colored = [&](uint8_t band) { return band < 16 ? palette.terracotta[band] : surface.hardClay; };
                        const int pillarFloor = SatCast(pillarTop) - (pillarTop < static_cast<float>(SatCast(pillarTop)) ? 1 : 0) - minY;
                        int run = -1, count = 0;
                        uint16_t midVar = materials.mid;
                        bool inSand = false, stop = false;
                        for (int y = height - 1; y >= lower; y--) {
                            uint16_t b = column.GetLocal(y);
                            if (b == palette.bedrock)
                                break;
                            const bool wasAir = b == palette.air;
                            const bool filled = y <= pillarFloor && !stop && wasAir;
                            if (filled) {
                                b = palette.stone;
                                column.SetLocal(y, b);
                                stop = false;
                            } else {
                                stop = stop || !wasAir;
                            }
                            if (count >= 15)
                                continue;
                            if (b == palette.air) {
                                run = -1;
                            } else if (b == palette.stone) {
                                if (run == -1) {
                                    const uint16_t candidate = sea + 1 < y ? midVar : materials.mid;
                                    if (sea - 4 <= y)
                                        midVar = candidate;
                                    if (runDepth < 1)
                                        midVar = materials.foundation;
                                    run = std::max(y - sea, 0) + runDepth;
                                    inSand = false;
                                    uint16_t placed;
                                    if (y < sea - 1) {
                                        placed = midVar == palette.terracotta[0] ? palette.terracotta[1] : midVar;
                                    } else if (!surface.hasForest || y <= sea + 33 + runDepth * 2) {
                                        if (y <= sea + 10 + runDepth) {
                                            inSand = true;
                                            placed = materials.top;
                                        } else {
                                            const bool inBands = y <= sea + 192 && sea < y;
                                            const uint8_t band = inBands && cosine <= 0.0f ? bandColor(y) : inBands ? uint8_t(16) : uint8_t(1);
                                            placed = colored(band);
                                        }
                                    } else {
                                        placed = cosine <= 0.0f ? palette.grassBlock : palette.coarseDirt;
                                    }
                                    column.SetLocal(y, placed);
                                } else if (run > 0) {
                                    run--;
                                    column.SetLocal(y, inSand ? palette.terracotta[1] : colored(bandColor(y)));
                                }
                                if (!filled)
                                    count++;
                            }
                        }
                        break;
                    }
                    case BuilderKind::NoiseGradient:
                        break;
                }
            }
        return lowerBounds;
    }

    void SurfacePass::BuildGradients(ProtoChunk& chunk, const BiomeVolume& volume, const std::array<int, 256>& lowerBounds) const {
        const int minY = chunk.minY;
        for (int lx = 0; lx < 16; lx++)
            for (int lz = 0; lz < 16; lz++) {
                const int top = lowerBounds[static_cast<size_t>(lz * 16 + lx)];
                int first = minY;
                while (first <= top) {
                    const int id = volume.IdAt(lx, first, lz);
                    int last = first;
                    while (last < top && volume.IdAt(lx, last + 1, lz) == id)
                        last++;
                    const auto it = biomes.find(id);
                    if (it != biomes.end() && it->second->kind == BuilderKind::NoiseGradient) {
                        const BiomeSurface& s = *it->second;
                        Column column{chunk, lx, lz};
                        const int x = chunk.MinX() + lx, z = chunk.MinZ() + lz;
                        for (int y = last - 1; y >= first; y--) {
                            const uint16_t b = column.Get(y);
                            if (b == palette.air || palette.IsLiquid(b) ||
                                std::find(s.nonReplaceable.begin(), s.nonReplaceable.end(), b) != s.nonReplaceable.end())
                                continue;
                            const float value = std::clamp(s.gradientNoise->Value(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)), -1.0f, 1.0f);
                            for (const Specifier& sp : s.specifiers)
                                if (sp.low < value && value <= sp.high && sp.block != palette.air) {
                                    column.Set(y, sp.block);
                                    break;
                                }
                        }
                    }
                    first = last + 1;
                }
            }
    }

    void SurfacePass::Build(const BedrockTerrain& terrain, ProtoChunk& chunk, const BiomeVolume& volume) const {
        const auto lower = BuildColumns(terrain, chunk, volume);
        BuildGradients(chunk, volume, lower);
    }
}
