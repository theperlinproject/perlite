#pragma once
// From noise to blocks: the corner columns of a chunk (terrain density plus the noodle-cave and ore-vein
// noises at each cell corner), the aquifer, and the fill that interpolates them into stone, deepslate, ore
// veins, water and lava.

#include "world/level/levelgen/bedrock/Terrain.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace perlite::bedrock {

    // Block ids the generator uses, resolved once from the block catalog.
    struct GenBlocks {
        uint16_t air = 0, stone, deepslate, water, lava, bedrock, copperOre, rawCopper, granite, deepslateIronOre, rawIron, tuff;
        static const GenBlocks& Get();
    };

    struct FluidTick {
        uint8_t x, z;
        int16_t y;
        bool lava;
    };

    // A chunk while it is generated: dense block ids bottom up, and the height above the highest solid block.
    struct ProtoChunk {
        int x = 0, z = 0;
        int minY = -64, height = 384;
        std::vector<uint16_t> blocks;  // (y - minY) * 256 + (lz << 4 | lx)
        std::array<int16_t, 256> tops{};
        std::vector<FluidTick> fluidTicks;
        ProtoChunk(int x, int z, int minY, int height) : x(x), z(z), minY(minY), height(height), blocks(static_cast<size_t>(height) * 256, 0) {
            tops.fill(static_cast<int16_t>(minY));
        }
        uint16_t Get(int lx, int y, int lz) const {
            return y < minY || y >= minY + height ? 0 : blocks[static_cast<size_t>(y - minY) * 256 + static_cast<size_t>((lz << 4) | lx)];
        }
        void Set(int lx, int y, int lz, uint16_t b) {
            if (y < minY || y >= minY + height)
                return;
            blocks[static_cast<size_t>(y - minY) * 256 + static_cast<size_t>((lz << 4) | lx)] = b;
        }
        int MinX() const { return x * 16; }
        int MinZ() const { return z * 16; }
    };

    struct Corner {
        float main = 0, noodle = 1, noodleThickness = 1, noodleRidgeA = 1, noodleRidgeB = 1, veinToggle = 1, veinA = 1, veinB = 1;
    };

    // Corners of one chunk's 4x4 cell columns (the west and north faces of its cells).
    struct CornerColumns {
        std::vector<Corner> corners;  // ((cz * 4 + cx) * levels + cy)
        int levels = 0;
        const Corner* Column(int cx, int cz) const { return corners.data() + static_cast<size_t>((cz * 4 + cx) * levels); }
    };

    // The 5x5 corner columns a chunk interpolates between: its own and the first column of its east, south and
    // south-east neighbours.
    struct NoiseChunk {
        std::vector<Corner> corners;
        int sizeY = 0;
        static NoiseChunk Assemble(const CornerColumns& own, const CornerColumns& east, const CornerColumns& south,
                                   const CornerColumns& southEast);
        const Corner& At(int cx, int cy, int cz) const { return corners[static_cast<size_t>((cz * 5 + cx) * sizeY + cy)]; }
    };

    class BufferNoises {
    public:
        explicit BufferNoises(const PositionalFactory& factory);
        Corner At(float density, float x, float y, float z) const;

    private:
        NormalNoise noodle, noodleThickness, noodleRidgeA, noodleRidgeB, veininess, veinA, veinB;
    };

    CornerColumns MakeCornerColumns(const BedrockTerrain& terrain, const BufferNoises& noises, int chunkX, int chunkZ);

    // Preliminary surface heights by quart, as the aquifer reads them around a chunk.
    using SurfaceAt = std::function<int(int quartX, int quartZ)>;

    struct AquiferNoises {
        explicit AquiferNoises(const PositionalFactory& root);
        NormalNoise barrier, floodedness, spread, lava;
        PositionalFactory factory;
    };

    class TerrainFill {
    public:
        TerrainFill(const PositionalFactory& root, const GenBlocks& blocks);
        // Fills chunk (all air) from the noise; returns the fluid ticks the aquifer asks for.
        void Run(const BedrockTerrain& terrain, const NoiseChunk& noise, ProtoChunk& chunk, const SurfaceAt& surface) const;
        const AquiferNoises& Aquifer() const { return aquifer; }

    private:
        uint16_t DefaultBlock(int x, int y, int z) const;
        uint16_t VeinBlock(float veininess, const std::function<std::pair<float, float>()>& ridges, int x, int y, int z,
                           uint16_t fallback) const;
        struct Vein {
            int minY, maxY;
            uint16_t ore, raw, filler;
        };
        const GenBlocks& blocks;
        Vein copper, iron;
        PositionalFactory depth, ore;
        NormalNoise oreGap;
        AquiferNoises aquifer;
    };

    // The aquifer of one chunk: which fluid fills air below which level, and the barrier between differing
    // neighbouring fluids. Also used by the carvers.
    class AquiferSampler {
    public:
        struct Substance {
            int level;
            bool lava;
            float barrier;
            bool scheduleTick;
        };
        AquiferSampler(const AquiferNoises& noises, const BedrockTerrain& terrain, const SurfaceAt& surface, int chunkX, int chunkZ);
        int DryAbove();
        Substance At(int x, int y, int z);

    private:
        struct GridPoint {
            int x, y, z;
        };
        struct Status {
            int level;
            bool lava;
        };
        size_t Slot(int gx, int gy, int gz) const;
        GridPoint Point(size_t slot, int gx, int gy, int gz);
        int Ceiling();
        Status StatusOf(size_t slot);
        Status Compute(GridPoint p);
        float Pressure(int x, int y, int z, Status a, Status b, std::optional<float>& noise) const;

        const AquiferNoises& noises;
        const BedrockTerrain& terrain;
        const SurfaceAt& surface;
        int chunkX, chunkZ;
        std::vector<std::optional<GridPoint>> points;
        std::vector<std::optional<Status>> statuses;
        std::optional<int> ceiling;
        int gridMinLevel, gridLevels;
        struct {
            std::optional<std::array<int, 3>> key;
            std::optional<std::pair<int, int>> column;
            std::array<size_t, 12> slots{};
            std::array<GridPoint, 12> points{};
            std::array<int64_t, 12> horizontal{};
        } near;
    };

    inline int FloorDiv(int a, int b) {
        const int q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }
}
