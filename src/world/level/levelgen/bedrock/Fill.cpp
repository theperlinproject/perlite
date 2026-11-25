#include "world/level/levelgen/bedrock/Fill.hpp"

#include "world/level/block/BlockCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace perlite::bedrock {

    const GenBlocks& GenBlocks::Get() {
        static const GenBlocks b = [] {
            const BlockCatalog& c = BlockCatalog::Instance();
            GenBlocks g;
            g.air = 0;
            g.stone = c.DefaultBlock("stone");
            g.deepslate = c.DefaultBlock("deepslate");
            g.water = c.DefaultBlock("water");
            g.lava = c.DefaultBlock("lava");
            g.bedrock = c.DefaultBlock("bedrock");
            g.copperOre = c.DefaultBlock("copper_ore");
            g.rawCopper = c.DefaultBlock("raw_copper_block");
            g.granite = c.DefaultBlock("granite");
            g.deepslateIronOre = c.DefaultBlock("deepslate_iron_ore");
            g.rawIron = c.DefaultBlock("raw_iron_block");
            g.tuff = c.DefaultBlock("tuff");
            return g;
        }();
        return b;
    }

    namespace {
        float ClampUnit(float v) { return v < -1.0f ? -1.0f : std::fmin(1.0f, v); }
        // partial_cmp based tests: a NaN operand passes each.
        bool NotLess(float a, float b) { return !(a < b); }
        bool NotGreater(float a, float b) { return !(a > b); }
        bool NotLessOrEqual(float a, float b) { return !(a <= b); }

        NormalNoise Make(const PositionalFactory& f, const char* name, int firstOctave) {
            Xoroshiro r = f.HashOf(std::string("minecraft:") + name);
            const double one[1] = {1.0};
            return NormalNoise(r, firstOctave, one);
        }
    }

    NoiseChunk NoiseChunk::Assemble(const CornerColumns& own, const CornerColumns& east, const CornerColumns& south,
                                    const CornerColumns& southEast) {
        NoiseChunk n;
        n.sizeY = own.levels;
        n.corners.reserve(static_cast<size_t>(25 * own.levels));
        for (int cz = 0; cz < 5; cz++)
            for (int cx = 0; cx < 5; cx++) {
                const Corner* col = cx < 4 && cz < 4 ? own.Column(cx, cz)
                                    : cx == 4 && cz < 4 ? east.Column(0, cz)
                                    : cx < 4            ? south.Column(cx, 0)
                                                        : southEast.Column(0, 0);
                n.corners.insert(n.corners.end(), col, col + own.levels);
            }
        return n;
    }

    BufferNoises::BufferNoises(const PositionalFactory& f)
        : noodle(Make(f, "noodle", -8)), noodleThickness(Make(f, "noodle_thickness", -8)),
          noodleRidgeA(Make(f, "noodle_ridge_a", -7)), noodleRidgeB(Make(f, "noodle_ridge_b", -7)),
          veininess(Make(f, "ore_veininess", -8)), veinA(Make(f, "ore_vein_a", -7)), veinB(Make(f, "ore_vein_b", -7)) {}

    Corner BufferNoises::At(float density, float x, float y, float z) const {
        Corner c;
        c.main = density;
        if (density < -150.0f)
            return c;
        constexpr float ridge = 2.6666667f, vein = 4.0f, veininessScale = 1.5f;
        c.noodle = ClampUnit(noodle.Value(x, y, z));
        c.noodleThickness = ClampUnit(noodleThickness.Value(x, y, z));
        c.noodleRidgeA = ClampUnit(noodleRidgeA.Value(x * ridge, y * ridge, z * ridge));
        c.noodleRidgeB = ClampUnit(noodleRidgeB.Value(x * ridge, y * ridge, z * ridge));
        c.veinToggle = ClampUnit(veininess.Value(x * veininessScale, y * veininessScale, z * veininessScale));
        c.veinA = ClampUnit(veinA.Value(x * vein, y * vein, z * vein));
        c.veinB = ClampUnit(veinB.Value(x * vein, y * vein, z * vein));
        return c;
    }

    CornerColumns MakeCornerColumns(const BedrockTerrain& terrain, const BufferNoises& noises, int chunkX, int chunkZ) {
        thread_local TerrainBuffers buffers;
        thread_local std::vector<float> densities;
        const int levels = terrain.CornerLevels();
        const int noiseLevels = terrain.NoiseLevels();
        const int minY = terrain.MinY();
        CornerColumns out;
        out.levels = levels;
        Corner unsampled;
        unsampled.main = -10.0f;
        out.corners.assign(static_cast<size_t>(16 * levels), unsampled);
        terrain.DensityCorners(chunkX, chunkZ, buffers, densities);
        for (int cz = 0; cz < 4; cz++)
            for (int cx = 0; cx < 4; cx++) {
                Corner* column = out.corners.data() + static_cast<size_t>((cz * 4 + cx) * levels);
                const size_t source = static_cast<size_t>((cx * kGridColumns + cz) * noiseLevels);
                const float x = static_cast<float>(chunkX * 16 + cx * kCellWidth);
                const float z = static_cast<float>(chunkZ * 16 + cz * kCellWidth);
                for (int cy = 0; cy < noiseLevels; cy++) {
                    const float y = static_cast<float>(minY + cy * kCellHeight);
                    column[cy] = noises.At(densities[source + static_cast<size_t>(cy)], x, y, z);
                }
            }
        return out;
    }

    AquiferNoises::AquiferNoises(const PositionalFactory& root)
        : barrier(Make(root, "aquifer_barrier", -3)), floodedness(Make(root, "aquifer_fluid_level_floodedness", -7)),
          spread(Make(root, "aquifer_fluid_level_spread", -5)), lava(Make(root, "aquifer_lava", -1)),
          factory(root.HashOf("minecraft:aquifer").ForkPositional()) {}

    // ---------------------------------------------------------------- aquifer

    namespace {
        constexpr int kNoFluid = -4096;
        constexpr int kCellXZ = 16, kCellY = 12;
        constexpr int kSpan = 3;
        constexpr int kDryAboveLevel = 5;

        float Similarity(uint64_t a, uint64_t b) {
            const uint64_t d = a > b ? a - b : b - a;
            return static_cast<float>(static_cast<int64_t>(d)) * -0.04f + 1.0f;
        }
    }

    AquiferSampler::AquiferSampler(const AquiferNoises& noises, const BedrockTerrain& terrain, const SurfaceAt& surface,
                                   int chunkX, int chunkZ)
        : noises(noises), terrain(terrain), surface(surface), chunkX(chunkX), chunkZ(chunkZ) {
        const DimensionDefinition def = terrain.Definition();
        gridMinLevel = FloorDiv(def.minY + 1, kCellY) - 1;
        const int highest = std::max(def.Top() - 72, terrain.SeaLevel()) + kDryAboveLevel;
        gridLevels = FloorDiv(highest + 1, kCellY) + 1 - gridMinLevel + 1;
        const size_t slots = static_cast<size_t>(kSpan * kSpan * gridLevels);
        points.assign(slots, std::nullopt);
        statuses.assign(slots, std::nullopt);
    }

    size_t AquiferSampler::Slot(int gx, int gy, int gz) const {
        const int ix = gx - (chunkX - 1), iy = gy - gridMinLevel, iz = gz - (chunkZ - 1);
        return static_cast<size_t>((ix * gridLevels + iy) * kSpan + iz);
    }

    AquiferSampler::GridPoint AquiferSampler::Point(size_t slot, int gx, int gy, int gz) {
        if (!points[slot]) {
            Xoroshiro r = noises.factory.At(gx, gy, gz);
            const int x = gx * kCellXZ + r.NextInt(10);
            const int y = gy * kCellY + r.NextInt(9);
            const int z = gz * kCellXZ + r.NextInt(10);
            points[slot] = GridPoint{x, y, z};
        }
        return *points[slot];
    }

    int AquiferSampler::Ceiling() {
        if (ceiling)
            return *ceiling;
        const int minX = chunkX * 16, minZ = chunkZ * 16;
        int highest = terrain.SeaLevel();
        for (int qx = (minX - 80) >> 2; qx <= (minX + 80) >> 2; qx++)
            for (int qz = (minZ - 80) >> 2; qz <= (minZ + 80) >> 2; qz++)
                highest = std::max(highest, surface(qx, qz));
        ceiling = highest;
        return highest;
    }

    AquiferSampler::Status AquiferSampler::StatusOf(size_t slot) {
        if (!statuses[slot])
            statuses[slot] = Compute(*points[slot]);
        return *statuses[slot];
    }

    AquiferSampler::Status AquiferSampler::Compute(GridPoint p) {
        static constexpr int offsets[13][2] = {{-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {-3, 0}, {-2, 0}, {-1, 0},
                                               {0, 0},   {1, 0},   {-2, 1}, {-1, 1}, {0, 1},  {1, 1}};
        const Status flooded{terrain.SeaLevel(), false};
        int minSurface = 32767;
        bool flagged = false;
        for (const auto& o : offsets) {
            const int s = surface((p.x + kCellXZ * o[0]) >> 2, (p.z + kCellXZ * o[1]) >> 2);
            const bool center = o[0] == 0 && o[1] == 0;
            if (center && s + 8 < p.y - 12)
                return flooded;
            if ((center || p.y + 4 > s) && s + 8 < terrain.SeaLevel()) {
                if (s < p.y + 4)
                    return flooded;
                flagged |= center;
            }
            minSurface = std::min(minSurface, s);
        }
        const auto [erosion, offset] = terrain.ErosionAndOffset(p.x >> 2, p.z >> 2);
        if (erosion < -0.225f && static_cast<float>(p.y) * -0.0078125f + 1.0f + offset + terrain.GlobalOffset() > 0.9f)
            return Status{kNoFluid, false};
        float dryness = 1.0f;
        if (flagged) {
            const float span = static_cast<float>(minSurface + 8 - p.y) * 0.015625f;
            dryness = span < 0.0f ? -0.0f : span <= 1.0f ? -((1.0f - span) + -1.0f) : 1.0f;
        }
        const float floodedness =
            ClampUnit(noises.floodedness.Value(static_cast<float>(p.x), static_cast<float>(p.y) * 0.67f, static_cast<float>(p.z)));
        if (floodedness > dryness * 1.1f + -0.3f)
            return flooded;
        if (floodedness < dryness * 1.2f + -0.8f)
            return Status{kNoFluid, false};
        const int sx = FloorDiv(p.x, 16), sy = FloorDiv(p.y, 40), sz = FloorDiv(p.z, 16);
        const float spread = noises.spread.Value(static_cast<float>(sx), static_cast<float>(sy) / 1.4f, static_cast<float>(sz));
        const float scaled = (spread * 10.0f) / 3.0f;
        const int level = SatCast(std::floor(scaled)) * 3 + sy * 40 + 20;
        const bool lava = level < -9 && std::fabs(noises.lava.Value(static_cast<float>(FloorDiv(p.x, 64)), static_cast<float>(FloorDiv(p.y, 40)),
                                                                     static_cast<float>(FloorDiv(p.z, 64)))) > 0.3f;
        return Status{std::min(level, minSurface), lava};
    }

    float AquiferSampler::Pressure(int x, int y, int z, Status a, Status b, std::optional<float>& noise) const {
        if (a.level > y && b.level > y && a.lava != b.lava)
            return 1.0f;
        if (a.level == b.level)
            return 0.0f;
        const float midpoint = static_cast<float>(a.level + b.level) * 0.5f;
        const float offset = (static_cast<float>(y) + 0.5f) - midpoint;
        const float quotient = static_cast<float>(std::abs(a.level - b.level)) * 0.5f - std::fabs(offset);
        float scaled;
        if (offset > 0.0f) {
            scaled = quotient / (quotient > 0.0f ? 1.5f : 2.5f);
        } else {
            const float shifted = quotient + 3.0f;
            scaled = shifted / (shifted > 0.0f ? 3.0f : 10.0f);
        }
        if (std::fabs(scaled) > 2.0f)
            return scaled;
        if (!noise)
            noise = noises.barrier.Value(static_cast<float>(x), static_cast<float>(y) * 0.5f, static_cast<float>(z));
        return scaled + *noise;
    }

    int AquiferSampler::DryAbove() { return std::max(terrain.SeaLevel() + kDryAboveLevel, Ceiling() + kDryAboveLevel); }

    AquiferSampler::Substance AquiferSampler::At(int x, int y, int z) {
        if (y <= -55)
            return Substance{-54, true, 0.0f, y == -55};
        if (!terrain.AquifersEnabled())
            return Substance{terrain.SeaLevel(), false, 0.0f, false};
        if (y >= terrain.SeaLevel() + kDryAboveLevel && y >= Ceiling() + kDryAboveLevel)
            return Substance{kNoFluid, false, 0.0f, false};
        const int gridX = FloorDiv(x - 5, kCellXZ), gridY = FloorDiv(y + 1, kCellY), gridZ = FloorDiv(z - 5, kCellXZ);
        const std::array<int, 3> key{gridX, gridY, gridZ};
        if (near.key != key) {
            size_t index = 0;
            for (int sx = 0; sx < 2; sx++)
                for (int sy = -1; sy <= 1; sy++)
                    for (int sz = 0; sz < 2; sz++) {
                        const int gx = gridX + sx, gy = gridY + sy, gz = gridZ + sz;
                        const size_t slot = Slot(gx, gy, gz);
                        near.points[index] = Point(slot, gx, gy, gz);
                        near.slots[index] = slot;
                        index++;
                    }
            near.key = key;
            near.column.reset();
        }
        if (near.column != std::pair<int, int>{x, z}) {
            for (size_t i = 0; i < 12; i++) {
                const int64_t dx = near.points[i].x - x, dz = near.points[i].z - z;
                near.horizontal[i] = dx * dx + dz * dz;
            }
            near.column = std::pair<int, int>{x, z};
        }
        std::pair<uint64_t, size_t> closest[3] = {{UINT64_MAX, 0}, {UINT64_MAX, 0}, {UINT64_MAX, 0}};
        for (size_t i = 0; i < 12; i++) {
            const int64_t dy = near.points[i].y - y;
            const uint64_t distance = static_cast<uint64_t>(near.horizontal[i] + dy * dy);
            const size_t slot = near.slots[i];
            if (closest[0].first >= distance) {
                closest[2] = closest[1];
                closest[1] = closest[0];
                closest[0] = {distance, slot};
            } else if (closest[1].first >= distance) {
                closest[2] = closest[1];
                closest[1] = {distance, slot};
            } else if (closest[2].first >= distance) {
                closest[2] = {distance, slot};
            }
        }
        const auto [d1, k1] = closest[0];
        const auto [d2, k2] = closest[1];
        const auto [d3, k3] = closest[2];
        const float nearSimilarity = Similarity(d1, d2);
        const Status first = StatusOf(k1);
        const bool scheduleTick = nearSimilarity > -0.4f;
        if (nearSimilarity <= -1.0f)
            return Substance{first.level, first.lava, 0.0f, scheduleTick};
        const Status second = StatusOf(k2);
        const Status third = StatusOf(k3);
        std::optional<float> noise;
        const float p12 = Pressure(x, y, z, first, second, noise);
        const float p13 = Pressure(x, y, z, first, third, noise);
        const float p23 = Pressure(x, y, z, second, third, noise);
        const float w13 = std::fmax(Similarity(d3, d1), 0.0f);
        const float w23 = std::fmax(Similarity(d3, d2), 0.0f);
        const float strongest = std::fmax(p12, std::fmax(w13 * p13, w23 * p23));
        const float nearW = std::fmax(nearSimilarity, 0.0f);
        return Substance{first.level, first.lava, std::fmax((nearW + nearW) * strongest, 0.0f), scheduleTick};
    }

    // ---------------------------------------------------------------- fill

    TerrainFill::TerrainFill(const PositionalFactory& root, const GenBlocks& blocks)
        : blocks(blocks), copper{0, 50, blocks.copperOre, blocks.rawCopper, blocks.granite},
          iron{-60, -8, blocks.deepslateIronOre, blocks.rawIron, blocks.tuff},
          depth(root.HashOf("minecraft:depth_based_layer").ForkPositional()), ore(root.HashOf("minecraft:ore").ForkPositional()),
          oreGap(Make(root, "ore_gap", -5)), aquifer(root) {}

    uint16_t TerrainFill::DefaultBlock(int x, int y, int z) const {
        if (y < 0)
            return blocks.deepslate;
        if (y > 8)
            return blocks.stone;
        const float h = static_cast<float>(y) * 0.125f;
        const float chance = h > 1.0f ? 0.0f : 1.0f - h;
        Xoroshiro r = depth.At(x, y, z);
        return chance > r.NextFloat() ? blocks.deepslate : blocks.stone;
    }

    uint16_t TerrainFill::VeinBlock(float veininess, const std::function<std::pair<float, float>()>& ridges, int x, int y,
                                    int z, uint16_t fallback) const {
        const Vein& vein = veininess > 0.0f ? copper : iron;
        const float above = static_cast<float>(y) - static_cast<float>(vein.minY);
        if (NotLessOrEqual(0.0f, above))
            return fallback;
        const float below = static_cast<float>(vein.maxY) - static_cast<float>(y);
        if (NotLessOrEqual(0.0f, below))
            return fallback;
        const float edgeDistance = std::fmin(above, below) / 20.0f;
        const float edge = 0.0f <= edgeDistance ? (edgeDistance <= 1.0f ? edgeDistance * 0.2f + -0.2f : 0.0f) : -0.2f;
        const float strength = std::fabs(veininess);
        if (NotLessOrEqual(0.4f, edge + strength))
            return fallback;
        Xoroshiro r = ore.At(x, y, z);
        if (NotLessOrEqual(r.NextFloat(), 0.7f))
            return fallback;
        const auto [ra, rb] = ridges();
        const float ridge = std::fmax(std::fabs(ra) + -0.08f, std::fabs(rb) + -0.08f);
        if (NotLess(ridge, 0.0f))
            return fallback;
        const float progress = (strength - 0.4f) / 0.20000002f;
        const float richness = 0.0f <= progress ? (progress <= 1.0f ? progress * 0.20000002f + 0.1f : 0.3f) : 0.1f;
        const float gap = oreGap.Value(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        if (r.NextFloat() < richness && -1.0f <= gap && std::fmin(gap, 1.0f) > -0.3f)
            return r.NextFloat() < 0.02f ? vein.raw : vein.ore;
        return vein.filler;
    }

    namespace {
        enum class CellKind { Air, Solid, Mixed };

        struct Range {
            float low = std::numeric_limits<float>::infinity(), high = -std::numeric_limits<float>::infinity();
            void Add(float v) {
                low = std::fmin(low, v);
                high = std::fmax(high, v);
            }
            bool ClearOfZero(float d) const { return low > d || high < -d; }
        };

        CellKind Classify(const NoiseChunk& noise, int cx, int cy, int cz, int yLow, bool veins) {
            Range main, noodle, ra, rb, va, vb;
            float veinStrength = 0.0f;
            for (int a = 0; a < 2; a++)
                for (int b = 0; b < 2; b++)
                    for (int c = 0; c < 2; c++) {
                        const Corner& k = noise.At(cx + a, cy + b, cz + c);
                        main.Add(k.main);
                        noodle.Add(k.noodle);
                        ra.Add(k.noodleRidgeA);
                        rb.Add(k.noodleRidgeB);
                        veinStrength = std::fmax(veinStrength, std::fabs(k.veinToggle));
                        va.Add(k.veinA);
                        vb.Add(k.veinB);
                    }
            if (main.high < -0.01f)
                return CellKind::Air;
            if (NotGreater(main.low, 0.01f))
                return CellKind::Mixed;
            const bool noodleAbsent = noodle.high < -0.01f || ra.ClearOfZero(0.07f) || rb.ClearOfZero(0.07f);
            if (!noodleAbsent)
                return CellKind::Mixed;
            const int yHigh = yLow + kCellHeight - 1;
            const bool veinAbsent = veinStrength < 0.39f || va.ClearOfZero(0.085f) || vb.ClearOfZero(0.085f);
            if (veins && yHigh >= -60 && yLow <= 50 && !veinAbsent)
                return CellKind::Mixed;
            return CellKind::Solid;
        }

        float Squeeze(float density) {
            const float t = std::clamp(density * 0.005f, -1.0f, 1.0f);
            return t * t * t / -24.0f + t * 0.5f;
        }

        struct Lane {
            float low, high;
            float At(float fy) const { return low + fy * (high - low); }
        };
    }

    void TerrainFill::Run(const BedrockTerrain& terrain, const NoiseChunk& noise, ProtoChunk& chunk, const SurfaceAt& surface) const {
        const bool veins = terrain.OreVeinsEnabled();
        const int minY = terrain.MinY();
        const int noiseCells = terrain.NoiseLevels() - 1;
        AquiferSampler sampler(aquifer, terrain, surface, chunk.x, chunk.z);
        const int dryAbove = sampler.DryAbove();
        std::vector<CellKind> kinds(static_cast<size_t>(16 * noiseCells));
        for (int index = 0; index < 16; index++)
            for (int cy = 0; cy < noiseCells; cy++)
                kinds[static_cast<size_t>(index * noiseCells + cy)] =
                    Classify(noise, index % 4, cy, index / 4, minY + cy * kCellHeight, veins);
        auto put = [&](int lx, int y, int lz, uint16_t block) {
            const size_t column = static_cast<size_t>((lz << 4) | lx);
            chunk.blocks[static_cast<size_t>(y - minY) * 256 + column] = block;
            if (block != blocks.air)
                chunk.tops[column] = std::max<int16_t>(chunk.tops[column], static_cast<int16_t>(y + 1));
        };
        for (int lz = 0; lz < 16; lz++)
            for (int lx = 0; lx < 16; lx++) {
                const int cx = lx / kCellWidth, cz = lz / kCellWidth;
                const float fx = static_cast<float>(lx % kCellWidth) * 0.25f;
                const float fz = static_cast<float>(lz % kCellWidth) * 0.25f;
                const int x = chunk.MinX() + lx, z = chunk.MinZ() + lz;
                for (int cy = 0; cy < noiseCells; cy++) {
                    const int yLow = minY + cy * kCellHeight;
                    const CellKind kind = kinds[static_cast<size_t>((cz * 4 + cx) * noiseCells + cy)];
                    if (kind == CellKind::Air && yLow >= dryAbove)
                        continue;
                    if (kind == CellKind::Solid) {
                        for (int ly = 0; ly < kCellHeight; ly++)
                            put(lx, yLow + ly, lz, DefaultBlock(x, yLow + ly, z));
                        continue;
                    }
                    const Corner* c[2][2][2];
                    for (int a = 0; a < 2; a++)
                        for (int b = 0; b < 2; b++)
                            for (int d = 0; d < 2; d++)
                                c[a][b][d] = &noise.At(cx + a, cy + b, cz + d);
                    auto lane = [&](float Corner::*field) {
                        auto alongZ = [&](int a, int b) {
                            const float nearV = c[a][b][0]->*field, farV = c[a][b][1]->*field;
                            return nearV + fz * (farV - nearV);
                        };
                        const float v00 = alongZ(0, 0), v01 = alongZ(0, 1), v10 = alongZ(1, 0), v11 = alongZ(1, 1);
                        return Lane{v00 + fx * (v10 - v00), v01 + fx * (v11 - v01)};
                    };
                    const Lane mainLane = lane(&Corner::main);
                    std::optional<Lane> toggleL, thicknessL, ridgeAL, ridgeBL, veinToggleL, veinAL, veinBL;
                    auto lazy = [&](std::optional<Lane>& slot, float Corner::*field) -> Lane {
                        if (!slot)
                            slot = lane(field);
                        return *slot;
                    };
                    for (int ly = 0; ly < kCellHeight; ly++) {
                        const int y = yLow + ly;
                        const float fy = static_cast<float>(ly) * 0.125f;
                        float value = Squeeze(mainLane.At(fy));
                        if (y >= minY + 4 && value > 0.0f) {
                            const float toggle = lazy(toggleL, &Corner::noodle).At(fy);
                            if (NotGreater(0.0f, toggle)) {
                                const float thickness = (lazy(thicknessL, &Corner::noodleThickness).At(fy) + 1.0f) * 0.5f;
                                const float width = thickness < 0.0f ? 0.05f : thickness > 1.0f ? 0.1f : thickness * 0.05f + 0.05f;
                                const float ra = std::fabs(lazy(ridgeAL, &Corner::noodleRidgeA).At(fy) * 1.5f) - width;
                                const float rb = std::fabs(lazy(ridgeBL, &Corner::noodleRidgeB).At(fy) * 1.5f) - width;
                                value = std::fmin(value, rb > ra ? rb : ra);
                            }
                        }
                        bool solid = value > 0.0f;
                        std::optional<AquiferSampler::Substance> substance;
                        if (!solid) {
                            const auto found = sampler.At(x, y, z);
                            solid = value + found.barrier > 0.0f;
                            substance = found;
                        }
                        if (solid) {
                            const uint16_t fallback = DefaultBlock(x, y, z);
                            uint16_t block = fallback;
                            if (veins && y >= -60 && y <= 50) {
                                const float veininess = lazy(veinToggleL, &Corner::veinToggle).At(fy);
                                block = VeinBlock(
                                    veininess,
                                    [&] { return std::pair<float, float>{lazy(veinAL, &Corner::veinA).At(fy), lazy(veinBL, &Corner::veinB).At(fy)}; },
                                    x, y, z, fallback);
                            }
                            put(lx, y, lz, block);
                        } else if (substance && y < substance->level) {
                            put(lx, y, lz, substance->lava ? blocks.lava : blocks.water);
                            if (substance->scheduleTick)
                                chunk.fluidTicks.push_back(FluidTick{static_cast<uint8_t>(lx), static_cast<uint8_t>(lz),
                                                                     static_cast<int16_t>(y), substance->lava});
                        }
                    }
                }
            }
    }
}
