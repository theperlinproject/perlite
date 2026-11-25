#pragma once
// What the Bedrock world generator needs to know about a block: whether it is solid, which faces it
// supports, its collision box, how it meets water and lava, what it may stand on, the block tags features
// test, and whether it survives where it is (can_survive) and how it reshapes when a neighbour changes.
//
// Block ids here are the server's runtime ids. Bit 15 marks a flooded block: the same block with water in
// its liquid layer (a waterlogged block that was placed in water).

#include "world/level/block/Blocks.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perlite::bedrock {

    using BlockId = uint16_t;
    constexpr BlockId kFloodedBit = 0x8000;
    constexpr BlockId kAirId = 0;

    struct IVec3 {
        int x = 0, y = 0, z = 0;
        constexpr IVec3 operator+(const IVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr IVec3 operator-(const IVec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr IVec3 operator*(int s) const { return {x * s, y * s, z * s}; }
        constexpr bool operator==(const IVec3&) const = default;
        constexpr IVec3 Up(int n = 1) const { return {x, y + n, z}; }
        constexpr IVec3 Down(int n = 1) const { return {x, y - n, z}; }
    };

    enum class Tag : uint8_t {
        Air,
        Corals,
        Dirt,
        FeaturesCannotReplace,
        Leaves,
        Logs,
        Mud,
        Replaceable,
        Sand,
        SulfurSpikeReplaceableBlocks,
        WallCorals,
        SupportsSugarCaneAdjacently,
        Count
    };

    enum class SupportFaces : uint8_t { None, Top, All };
    enum class LiquidTouch : uint8_t { Blocking, Broken };
    enum class PlacementFace : uint8_t { Down, Up, North, South, East, West };

    // How a block decides it can stay where it is.
    enum class Support : uint8_t {
        Anywhere,
        SugarCane,
        Cactus,
        CactusFlower,
        Mushroom,
        SturdyFloor,
        Seagrass,
        SolidFloor,
        Kelp,
        Propagule,
        SnowLayer,
        AnyFloor,
        SporeBlossom,
        SturdyCeiling,
        PaleHangingMoss,
        Waterlily,
        BigDripleaf,
        WallCoral,
        Coral,
        Placed,
    };

    enum class Direction : uint8_t { Down, Up, North, South, West, East };
    constexpr std::array<Direction, 6> kAllDirections = {Direction::Down, Direction::Up, Direction::North,
                                                         Direction::South, Direction::West, Direction::East};
    constexpr std::array<Direction, 4> kHorizontal = {Direction::North, Direction::East, Direction::South, Direction::West};
    IVec3 Offset(Direction d);
    Direction Opposite(Direction d);
    const char* DirectionName(Direction d);
    int VineBit(Direction face);
    int MultifaceBit(Direction face);
    Direction FromLegacyDirection(int value);

    struct BlockEntry {
        uint16_t kind = 0; // one per block name
        std::string name;
        StateList states;
        uint32_t tags = 0;
        bool solid = true;
        bool transparent = false;
        bool full = false;
        bool cube = false;
        bool vineSupport = false;
        bool bdsSolid = true;
        bool collisionCube = true;
        bool topCentre = false;
        bool raisedTop = false;
        bool liquid = false;
        bool waterloggable = false;
        bool flooded = false;
        bool replaceable = false;
        LiquidTouch waterTouch = LiquidTouch::Blocking;
        LiquidTouch lavaTouch = LiquidTouch::Blocking;
        int8_t upper = -1; // the upper (1) or lower (0) half of a two-block plant, -1 otherwise
        Support support = Support::Anywhere;
        SupportFaces faceSupport = SupportFaces::None;
        int16_t placement = -1; // rule list the block may be placed on
        int16_t denied = -1;    // blocks it may not be placed on
    };

    class Blocks {
    public:
        static const Blocks& Get();

        const BlockEntry& Entry(BlockId id) const;
        BlockId Id(std::string_view name, const StateList& states = {}) const;
        BlockId IdWith(std::string_view name, std::initializer_list<std::pair<const char*, StateValue>> states) const;
        uint16_t Kind(std::string_view name) const;

        bool Is(BlockId id, Tag tag) const { return (Entry(id).tags >> static_cast<int>(tag)) & 1; }
        bool IsAir(BlockId id) const { return Is(id, Tag::Air); }
        bool IsKind(BlockId id, uint16_t kind) const { return Entry(id).kind == kind; }
        bool SameKind(BlockId a, BlockId b) const { return Entry(a).kind == Entry(b).kind; }
        const std::string& NameOf(BlockId id) const { return Entry(id).name; }
        bool NameIs(BlockId id, std::string_view name) const { return Entry(id).name == name; }

        const StateValue* State(BlockId id, std::string_view key) const;
        bool Flag(BlockId id, std::string_view key) const;
        std::optional<int> Int(BlockId id, std::string_view key) const;
        const std::string* Text(BlockId id, std::string_view key) const;
        // The same block with one state changed; unchanged when the block has no such state.
        BlockId With(BlockId id, std::string_view key, const StateValue& value) const;
        BlockId Flooded(BlockId id) const { return static_cast<BlockId>(id | kFloodedBit); }
        static BlockId Runtime(BlockId id) { return static_cast<BlockId>(id & ~kFloodedBit); }

        bool PlacedOn(BlockId block, BlockId neighbour, PlacementFace face) const;
        bool DeniedOn(BlockId block, BlockId neighbour) const;

    private:
        Blocks();
        struct Impl;
        Impl* impl;
    };

    // Anything a block can be read from by world position.
    class BlockView {
    public:
        virtual ~BlockView() = default;
        virtual BlockId Get(const IVec3& pos) const = 0;
    };

    enum class Fluid : uint8_t { None, Water, Lava };
    Fluid FluidAt(BlockId block);
    Direction CoralFanFacing(BlockId fan);
    bool CanSurvive(const BlockView& view, BlockId block, const IVec3& pos);

    // The pale moss carpet's side shapes.
    class MossCarpet {
    public:
        static const MossCarpet& Get();
        uint16_t kind = 0;
        BlockId UpdateShape(const BlockView& view, BlockId state, const IVec3& pos) const;

    private:
        MossCarpet();
        std::vector<BlockId> states;
        int Decode(BlockId state) const;
    };

    // Shape updates of blocks whose neighbour changed (the game's updateShape), as generation runs them.
    struct Shapes {
        BlockId water = 0;
        bool packedIceHoldsSnow = true;
        bool doublePlantsKeepOrphans = true;
        BlockId Update(const BlockView& view, const IVec3& pos, BlockId state, Direction direction) const;
        BlockId UpdateFromNeighbours(const BlockView& view, const IVec3& pos) const;
    };

    // The generated block list, one entry per line (BlockTraitsData.cpp).
    extern const char* const kBlockTraitsLines[];
}
