#include "world/level/levelgen/bedrock/Carvers.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"

#include <cmath>
#include <mutex>
#include <set>
#include <string>

namespace perlite::bedrock {

    namespace {
        constexpr float kPi = 3.14159274101257324f;
        constexpr float kHalfPi = 1.57079637050628662f;
        float Sin(float a) { return static_cast<float>(std::sin(static_cast<double>(a))); }
        float Cos(float a) { return static_cast<float>(std::cos(static_cast<double>(a))); }
        bool NotLess(float a, float b) { return !(a < b); }
        bool NotGreater(float a, float b) { return !(a > b); }

        enum : uint8_t { kCave = 1, kCanyon = 2, kSand = 4, kDirt = 8, kGrassOrMycelium = 16, kGrass = 32, kKnown = 128 };

        struct CaveSettings {
            int skipCarveChance, heightLimit;
            float yScaleMin, yScaleMax, hMin, hMax, vMin, vMax, floorMin, floorMax, widthModifier;
            int caveBound, roomChance, extraTunnels, skipSegmentChance, steepChance;
        };
        constexpr CaveSettings kUnderground{33, 236, 0.1f, 0.9f, 0.7f, 1.4f, 0.8f, 1.3f, -1.0f, -0.4f, 0.0f, 40, 4, 4, 4, 6};
        constexpr CaveSettings kExtra{66, 103, 0.1f, 0.9f, 0.7f, 1.4f, 0.8f, 1.3f, -1.0f, -0.4f, 0.0f, 40, 4, 4, 4, 6};
        const CaveSettings* const kCarvers[2] = {&kUnderground, &kExtra};

        float Draw(float lo, float hi, MersenneTwister& r) { return lo == hi ? lo : lo + (hi - lo) * r.NextFloat(); }

        bool Starts(const CaveSettings& s, const std::array<uint32_t, 4>& w) {
            const int bound = static_cast<int>(w[0] % static_cast<uint32_t>(s.caveBound));
            const int bound2 = static_cast<int>(w[1] % static_cast<uint32_t>(bound + 1));
            const int count = static_cast<int>(w[2] % static_cast<uint32_t>(bound2 + 1));
            return count != 0 && w[3] % static_cast<uint32_t>(s.skipCarveChance) == 0;
        }

        int TunnelLength(MersenneTwister& r) { return 112 - r.NextInt(28); }
        float TunnelThickness(MersenneTwister& r) {
            const float first = r.NextFloat();
            const float doubled = first + first;
            float t = r.NextFloat() + doubled;
            if (r.NextInt(10) == 0) {
                const float a = r.NextFloat();
                const float b = r.NextFloat();
                t *= b * a * 3.0f + 1.0f;
            }
            return t;
        }

        struct Cave {
            float h, v, floor;
        };
        struct Tunnel {
            float thickness, yaw, pitch;
            int step, length;
        };

        void TunnelSegments(const CaveSettings& s, MersenneTwister& parent, std::array<float, 3> at, Tunnel t, Cave cave,
                            std::vector<Carvers::Segment>& out) {
            MersenneTwister r(parent.NextU32() >> 1);
            int branchStep = 0;
            if (t.length != 1)
                branchStep = static_cast<int>(r.NextU32() % (static_cast<uint32_t>(t.length) >> 1));
            const float pitchDecay = r.NextInt(s.steepChance) != 0 ? 0.7f : 0.92f;
            if (t.step >= t.length)
                return;
            branchStep += t.length >> 2;
            float pitchChange = 0.0f, yawChange = 0.0f;
            for (;;) {
                const float sine = Sin(static_cast<float>(t.step) * kPi / static_cast<float>(t.length));
                const float width = s.widthModifier + t.thickness;
                const float cp = Cos(t.pitch), sp = Sin(t.pitch);
                at[0] += Cos(t.yaw) * cp;
                at[1] += sp;
                at[2] += Sin(t.yaw) * cp;
                const float decayedPitch = t.pitch * pitchDecay;
                const float decayedPitchChange = 0.9f * pitchChange;
                const float decayedYawChange = 0.75f * yawChange;
                const float pa = r.NextFloat(), pb = r.NextFloat(), ps = r.NextFloat(), ya = r.NextFloat(), yb = r.NextFloat(), ys = r.NextFloat();
                const float radius = sine * width + 1.5f;
                t.pitch = pitchChange * 0.1f + decayedPitch;
                t.yaw += yawChange * 0.1f;
                const float yawDelta = ys * (ya - yb) * 4.0f;
                if (t.thickness > 1.0f && branchStep == t.step) {
                    for (const float side : {-kHalfPi, kHalfPi}) {
                        const float thickness = r.NextFloat() * 0.5f + 0.5f;
                        TunnelSegments(s, r, at, Tunnel{thickness, t.yaw + side, t.pitch / 3.0f, branchStep, t.length}, cave, out);
                    }
                    return;
                }
                if (r.NextInt(s.skipSegmentChance) != 0)
                    out.push_back(Carvers::Segment{{at[0], at[1], at[2]}, radius, radius, cave.floor, t.step, t.length, t.thickness});
                if (t.step + 1 >= t.length)
                    return;
                const float pd = ps * (pa - pb);
                pitchChange = decayedPitchChange + (pd + pd);
                yawChange = decayedYawChange + yawDelta;
                t.step++;
            }
        }

        void CaveSegments(const CaveSettings& s, MersenneTwister& r, int chunkX, int chunkZ, std::vector<Carvers::Segment>& out) {
            out.clear();
            const int bound = r.NextInt(s.caveBound);
            const int bound2 = r.NextInt(bound + 1);
            const int count = r.NextInt(bound2 + 1);
            if (count == 0 || r.NextInt(s.skipCarveChance) != 0)
                return;
            for (int n = 0; n < count; n++) {
                const int z = r.NextInt(16) + chunkZ * 16;
                const int y = r.NextInt(s.heightLimit) + 8;
                const int x = r.NextInt(16) + chunkX * 16;
                const std::array<float, 3> origin{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                Cave cave;
                cave.h = Draw(s.hMin, s.hMax, r);
                cave.v = Draw(s.vMin, s.vMax, r);
                cave.floor = Draw(s.floorMin, s.floorMax, r);
                int tunnels = 1;
                if (r.NextInt(s.roomChance) == 0) {
                    const float thickness = r.NextFloat() * 6.0f + 1.0f;
                    MersenneTwister room(r.NextU32() >> 1);
                    const float yScale = Draw(s.yScaleMin, s.yScaleMax, room);
                    const int length = TunnelLength(room);
                    const int step = length >> 1;
                    const float radius = Sin(static_cast<float>(step) * kPi / static_cast<float>(length)) * (s.widthModifier + thickness) + 1.5f;
                    out.push_back(Carvers::Segment{{origin[0], origin[1], origin[2]}, radius * cave.h, yScale * radius * cave.v, cave.floor, step, length, thickness});
                    tunnels += r.NextInt(s.extraTunnels);
                }
                for (int t = 0; t < tunnels; t++) {
                    float yaw = r.NextFloat() * kPi;
                    yaw = yaw + yaw;
                    float pitch = r.NextFloat() - 0.5f;
                    pitch = (pitch + pitch) * 0.125f;
                    const float thickness = TunnelThickness(r);
                    const int length = TunnelLength(r);
                    TunnelSegments(s, r, origin, Tunnel{thickness, yaw, pitch, 0, length}, cave, out);
                }
            }
        }

        std::array<float, 320> WidthProfile(MersenneTwister& first, MersenneTwister& second) {
            std::array<float, 320> w{};
            const float a = first.NextFloat();
            float value = first.NextFloat() * a + 1.0f;
            w[0] = value * value;
            for (size_t i = 1; i < 320; i++) {
                MersenneTwister& r = i < 128 ? first : second;
                if (r.NextInt(3) == 0) {
                    const float b = r.NextFloat();
                    value = r.NextFloat() * b + 1.0f;
                }
                w[i] = value * value;
            }
            return w;
        }

        std::unique_ptr<Carvers::Canyon> MakeCanyon(MersenneTwister& r, int chunkX, int chunkZ, int minY) {
            if (r.NextInt(100) != 0)
                return nullptr;
            const int x = r.NextInt(16) + chunkX * 16;
            const int y = r.NextInt(58) - minY + 10;
            r.Skip(2);
            const int z = r.NextInt(16) + chunkZ * 16;
            float yaw = r.NextFloat() * kPi;
            float pitch = r.NextFloat() - 0.5f;
            const float first = r.NextFloat() * 3.0f;
            const float thickness = r.NextFloat() * 3.0f + first;
            r.NextFloat();
            const uint32_t seed = r.NextU32() >> 1;
            MersenneTwister walk(seed);
            const int length = 120 - static_cast<int>(walk.NextU32() % 30u);
            MersenneTwister second(seed);
            auto canyon = std::make_unique<Carvers::Canyon>();
            canyon->thickness = thickness;
            canyon->widths = WidthProfile(walk, second);
            float at[3] = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
            yaw *= 2.0f;
            pitch *= 0.25f;
            float pitchChange = 0.0f, yawChange = 0.0f;
            for (int step = 0; step < length; step++) {
                const float sine = Sin(static_cast<float>(step) * kPi / static_cast<float>(length));
                const float baseRadius = sine * thickness + 1.5f;
                const float hd = walk.NextFloat();
                const float vd = walk.NextFloat();
                const float cp = Cos(pitch), sp = Sin(pitch);
                at[0] += Cos(yaw) * cp;
                at[1] += sp;
                at[2] += Sin(yaw) * cp;
                const float decayedPitch = pitch * 0.7f;
                const float decayedPitchChange = pitchChange * 0.8f;
                const float decayedYawChange = yawChange * 0.5f;
                const float pa = walk.NextFloat(), pb = walk.NextFloat(), ps = walk.NextFloat();
                const float ya = walk.NextFloat();
                const float yb = walk.NextFloat();
                const float yawDifference = ya - yb;
                const float yawDelta = walk.NextFloat() * yawDifference * 4.0f;
                if ((walk.NextU32() & 3u) != 0) {
                    const float hr = (hd * 0.25f + 0.75f) * baseRadius;
                    const float vr = (vd * 0.25f + 0.75f) * (baseRadius * 4.0f);
                    canyon->steps.push_back(Carvers::Step{{at[0], at[1], at[2]}, hr, vr, length - step});
                }
                pitch = decayedPitch + pitchChange * 0.05f;
                yaw += yawChange * 0.05f;
                const float pd = ps * (pa - pb);
                pitchChange = decayedPitchChange + (pd + pd);
                yawChange = decayedYawChange + yawDelta;
            }
            return canyon;
        }

        bool Reaches(const Carvers::Segment& s, int chunkX, int chunkZ) {
            const float mx = static_cast<float>(chunkX * 16 + 8), mz = static_cast<float>(chunkZ * 16 + 8);
            const float dx = s.center[0] - mx, dz = s.center[2] - mz;
            const int remaining = s.length - s.step;
            const float distance = (dz * dz + dx * dx) - static_cast<float>(remaining * remaining);
            const float reach = s.thickness + 2.0f + 16.0f;
            if (reach * reach < distance)
                return false;
            const float margin = s.horizontalRadius + s.horizontalRadius + 16.0f;
            const float fx = static_cast<float>(SatCast(std::floor(s.center[0])));
            const float fz = static_cast<float>(SatCast(std::floor(s.center[2])));
            return !(mx - margin > fx || fx > mx + margin || mz - margin > fz || fz > mz + margin);
        }

        std::pair<int, int> HorizontalBounds(float center, float radius, int origin) {
            const int low = std::max(SatCast(std::floor(center - radius)) - origin - 1, 0);
            const int high = std::min(SatCast(std::floor(center + radius)) - origin, 15) + 1;
            return {low, high};
        }

        std::pair<int, int> CanyonHorizontalBounds(float center, float radius, int origin) {
            const int low = SatCast(std::floor(center - radius) - static_cast<float>(origin) + -1.0f);
            const int high = SatCast(std::floor(center + radius) - static_cast<float>(origin) + 1.0f);
            return {std::max(low, 0), std::min(high, 16)};
        }

        struct FluidCells {
            const AquiferNoises& noises;
            const BedrockTerrain& terrain;
            const SurfaceAt& surface;
            ProtoChunk& chunk;
            std::unique_ptr<AquiferSampler> sampler;
            uint16_t air, water, lava;
            void Empty(int lx, int y, int lz) {
                if (!sampler)
                    sampler = std::make_unique<AquiferSampler>(noises, terrain, surface, chunk.x, chunk.z);
                const auto s = sampler->At((chunk.x << 4) + lx, y, (chunk.z << 4) + lz);
                if (s.barrier > 0.0f)
                    return;
                const uint16_t block = y >= s.level ? air : s.lava ? lava : water;
                if (block != air && s.scheduleTick)
                    chunk.fluidTicks.push_back(FluidTick{static_cast<uint8_t>(lx), static_cast<uint8_t>(lz), static_cast<int16_t>(y), s.lava});
                chunk.Set(lx, y, lz, block);
            }
        };
    }

    Carvers::Carvers(uint32_t levelSeed) : levelSeed(levelSeed), multipliers(LargeFeatureMultipliers(levelSeed)) {
        const BlockCatalog& c = BlockCatalog::Instance();
        air = 0;
        sandstone = c.DefaultBlock("sandstone");
        grass = c.DefaultBlock("grass_block");
    }

    uint8_t Carvers::Flags(uint16_t block) const {
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        if (block < flags.size() && (flags[block] & kKnown))
            return flags[block];
        static const std::set<std::string> shared = {"stone", "dirt", "coarse_dirt", "podzol", "grass_block", "mycelium", "snow_layer",
                                                     "packed_ice", "deepslate", "calcite", "sand", "red_sand", "gravel", "tuff",
                                                     "dirt_with_roots", "suspicious_sand", "sandstone", "chiseled_sandstone", "cut_sandstone",
                                                     "smooth_sandstone", "red_sandstone", "chiseled_red_sandstone", "cut_red_sandstone",
                                                     "smooth_red_sandstone", "iron_ore", "deepslate_iron_ore", "raw_iron_block", "copper_ore",
                                                     "deepslate_copper_ore", "raw_copper_block"};
        static const std::set<std::string> canyonOnly = {"granite", "diorite", "andesite", "mud", "snow", "sulfur", "cinnabar", "hardened_clay"};
        std::string name = BlockRegistry::Instance().Name(block);
        if (name.starts_with("minecraft:"))
            name = name.substr(10);
        uint8_t f = kKnown;
        if (shared.contains(name))
            f |= kCave | kCanyon;
        if (name.ends_with("_glazed_terracotta"))
            f |= kCave;
        else if (name.ends_with("_terracotta") || canyonOnly.contains(name))
            f |= kCanyon;
        if (name == "sand" || name == "red_sand" || name == "suspicious_sand")
            f |= kSand;
        if (name == "dirt" || name == "coarse_dirt")
            f |= kDirt;
        if (name == "grass_block" || name == "mycelium")
            f |= kGrassOrMycelium;
        if (name == "grass_block")
            f |= kGrass;
        if (flags.size() <= block)
            flags.resize(static_cast<size_t>(block) + 1, 0);
        flags[block] = f;
        return f;
    }

    Carvers::Plan Carvers::PlanAt(int x, int z, int minY) const {
        const uint32_t seed = DecorationSeed(levelSeed, multipliers, x, z);
        const auto words = MersenneTwister::LeadingWords<4>(seed);
        Plan plan;
        for (int i = 0; i < 2; i++)
            if (Starts(*kCarvers[i], words)) {
                MersenneTwister r(seed);
                CaveSegments(*kCarvers[i], r, x, z, plan.caves[i]);
            }
        if (words[0] % 100u == 0) {
            MersenneTwister r(seed);
            plan.canyon = MakeCanyon(r, x, z, minY);
        }
        return plan;
    }

    void Carvers::Carve(const AquiferNoises& noises, const BedrockTerrain& terrain, const SurfaceAt& surface, ProtoChunk& chunk) const {
        const GenBlocks& gb = GenBlocks::Get();
        FluidCells fluids{noises, terrain, surface, chunk, nullptr, air, gb.water, gb.lava};
        const int cx = chunk.x, cz = chunk.z, minY = chunk.minY;
        const int maxY = chunk.minY + chunk.height - 1;
        std::vector<Plan> plans;
        plans.reserve(17 * 17);
        for (int x = cx - 8; x <= cx + 8; x++)
            for (int z = cz - 8; z <= cz + 8; z++)
                plans.push_back(PlanAt(x, z, minY));
        for (int index = 0; index < 2; index++) {
            const int heightCap = kCarvers[index]->heightLimit;
            for (const Plan& plan : plans)
                for (const Segment& s : plan.caves[index]) {
                    if (!Reaches(s, cx, cz))
                        continue;
                    const float x = s.center[0], y = s.center[1], z = s.center[2];
                    const int top = SatCast(std::floor(y + s.verticalRadius));
                    const int high = heightCap <= top ? heightCap : std::max(top, 0) + 1;
                    const int bottom = SatCast(std::floor(y - s.verticalRadius)) - 1;
                    const int low = bottom > high ? high : std::max(bottom, 1);
                    const auto bx = HorizontalBounds(x, s.horizontalRadius, cx * 16);
                    const auto bz = HorizontalBounds(z, s.horizontalRadius, cz * 16);
                    for (int lx = bx.first; lx < bx.second; lx++) {
                        const float dx = ((static_cast<float>(cx * 16 + lx) + 0.5f) - x) / s.horizontalRadius;
                        const float dx2 = dx * dx;
                        for (int lz = bz.first; lz < bz.second; lz++) {
                            const float dz = ((static_cast<float>(cz * 16 + lz) + 0.5f) - z) / s.horizontalRadius;
                            const float dz2 = dz * dz;
                            if (NotGreater(1.0f, dx2 + dz2))
                                continue;
                            for (int ly = high; ly >= low + 1; ly--) {
                                const float dy = ((static_cast<float>(ly - 1) + 0.5f) - y) / s.verticalRadius;
                                if (NotGreater(dy, s.floorLevel) || NotLess(dy * dy + dx2 + dz2, 1.0f))
                                    continue;
                                const int wy = ly + minY;
                                const uint16_t original = chunk.Get(lx, wy, lz);
                                if (!(Flags(original) & kCave))
                                    continue;
                                if (wy + 3 <= maxY - 64 && (Flags(chunk.Get(lx, wy + 1, lz)) & kSand) && (Flags(chunk.Get(lx, wy + 2, lz)) & kSand) &&
                                    (Flags(chunk.Get(lx, wy + 3, lz)) & kSand))
                                    chunk.Set(lx, wy + 1, lz, sandstone);
                                fluids.Empty(lx, wy, lz);
                                if ((Flags(original) & kGrassOrMycelium) && (Flags(chunk.Get(lx, wy - 1, lz)) & kDirt))
                                    chunk.Set(lx, wy - 1, lz, original);
                            }
                        }
                    }
                }
        }
        const float mx = static_cast<float>(cx * 16 + 8), mz = static_cast<float>(cz * 16 + 8);
        for (const Plan& plan : plans) {
            if (!plan.canyon)
                continue;
            const Canyon& canyon = *plan.canyon;
            const float reach = 2.0f + canyon.thickness + 16.0f;
            for (const Step& st : canyon.steps) {
                const float dx = st.center[0] - mx, dz = st.center[2] - mz;
                const float remaining = static_cast<float>(st.remaining);
                if ((dz * dz + dx * dx) - remaining * remaining > reach * reach)
                    break;
                const float margin = st.horizontalRadius + st.horizontalRadius;
                const float x = st.center[0], z = st.center[2], y = st.center[1];
                if ((mx - 16.0f) - margin > x || (mz - 16.0f) - margin > z || x > (mx + 16.0f) + margin || z > margin + (16.0f + mz))
                    continue;
                const auto bx = CanyonHorizontalBounds(x, st.horizontalRadius, cx * 16);
                const auto bz = CanyonHorizontalBounds(z, st.horizontalRadius, cz * 16);
                const int bottom = SatCast(std::floor(y - st.verticalRadius) + -1.0f);
                const int top = SatCast(std::floor(y + st.verticalRadius) + 1.0f);
                const int ceiling = chunk.height - 72;
                const int yHigh = ceiling < top ? ceiling : std::max(top, 1);
                const int yLow = yHigh < bottom ? yHigh : std::max(bottom, 1);
                for (int lx = bx.first; lx < bx.second; lx++) {
                    const float ddx = ((static_cast<float>(cx * 16 + lx) + 0.5f) - x) / st.horizontalRadius;
                    for (int lz = bz.first; lz < bz.second; lz++) {
                        const float ddz = ((static_cast<float>(cz * 16 + lz) + 0.5f) - z) / st.horizontalRadius;
                        const float horizontal = ddz * ddz + ddx * ddx;
                        if (NotLess(horizontal, 1.0f))
                            continue;
                        bool metGrass = false;
                        for (int ly = yHigh; ly >= yLow + 1; ly--) {
                            const float dy = ((static_cast<float>(ly - 1) + 0.5f) - y) / st.verticalRadius;
                            const int wi = ly - 2;
                            if (wi < 0 || wi >= 320)
                                continue;
                            if (NotLess((dy * dy) / 6.0f + canyon.widths[static_cast<size_t>(wi)] * horizontal, 1.0f))
                                continue;
                            const int wy = ly + minY;
                            const uint16_t block = chunk.Get(lx, wy, lz);
                            metGrass |= (Flags(block) & kGrass) != 0;
                            if (!(Flags(block) & kCanyon))
                                continue;
                            fluids.Empty(lx, wy, lz);
                            if (metGrass && (Flags(chunk.Get(lx, wy - 1, lz)) & kDirt))
                                chunk.Set(lx, wy - 1, lz, grass);
                        }
                    }
                }
            }
        }
    }
}
