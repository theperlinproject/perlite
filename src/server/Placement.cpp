// Placing blocks the way the client predicts them: the block faces the way the player looks or the
// face they clicked, slabs and stairs go top or bottom by where the face was clicked, doors, beds and
// tall plants take two blocks, and a block can replace water, grass or fire and keep the water when
// it can hold it.
#include "server/Server.hpp"

#include "network/session/Session.hpp"
#include "plugin/PluginHost.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/block/Waterlog.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace perlite {
    namespace {
        // Faces as the client numbers them: down, up, north (z-), south (z+), west (x-), east (x+).
        constexpr int kOffset[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        constexpr int kDown = 0, kUp = 1, kNorth = 2, kSouth = 3, kWest = 4, kEast = 5;
        const char* const kFaceName[6] = {"down", "up", "north", "south", "west", "east"};

        int Opposite(int face) { return face ^ 1; }

        // The horizontal way the player looks (yaw 0 looks south, 90 west).
        int Looking(float yaw) {
            switch (static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3) {
            case 0:
                return kSouth;
            case 1:
                return kWest;
            case 2:
                return kNorth;
            default:
                return kEast;
            }
        }

        // The nearest way the player looks, up and down included.
        int Looking3d(float yaw, float pitch) {
            if (pitch > 45.0f)
                return kDown;
            if (pitch < -45.0f)
                return kUp;
            return Looking(yaw);
        }

        int Clockwise(int face) {
            switch (face) {
            case kNorth:
                return kEast;
            case kEast:
                return kSouth;
            case kSouth:
                return kWest;
            default:
                return kNorth;
            }
        }

        // The old horizontal numbering: south 0, west 1, north 2, east 3.
        int32_t Legacy(int face) {
            switch (face) {
            case kSouth:
                return 0;
            case kWest:
                return 1;
            case kNorth:
                return 2;
            default:
                return 3;
            }
        }

        // Stairs: east 0, west 1, south 2, north 3.
        int32_t Weirdo(int face) {
            switch (face) {
            case kEast:
                return 0;
            case kWest:
                return 1;
            case kSouth:
                return 2;
            default:
                return 3;
            }
        }

        std::string Bare(const std::string& name) { return name.substr(name.find(':') + 1); }

        bool Has(const StateList& states, const std::string& key) {
            for (const auto& [k, v] : states)
                if (k == key)
                    return true;
            return false;
        }

        void Put(StateList& states, const std::string& key, StateValue value) {
            for (auto& [k, v] : states)
                if (k == key)
                    v = std::move(value);
        }

        bool BoolState(uint16_t id, const std::string& key) {
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == key) {
                    if (const bool* b = std::get_if<bool>(&v))
                        return *b;
                    if (const int32_t* i = std::get_if<int32_t>(&v))
                        return *i != 0;
                }
            return false;
        }

        std::string TextState(uint16_t id, const std::string& key) {
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == key)
                    if (const std::string* s = std::get_if<std::string>(&v))
                        return *s;
            return {};
        }

        // A state the client knows, or the block's default when the combination does not exist.
        uint16_t Valid(const std::string& name, StateList states, uint16_t fallback) {
            const uint16_t id = BlockRegistry::Instance().Intern(name, std::move(states));
            return BlockCatalog::Instance().KnownHash(BlockRegistry::Instance().Hash(id)) ? id : fallback;
        }

        uint16_t Named(const std::string& name) { return BlockCatalog::Instance().DefaultBlock(name); }

        // Blocks a placed block takes the place of.
        bool Replaceable(uint16_t id) {
            if (id == BlockRegistry::kAir)
                return true;
            const std::string n = Bare(BlockRegistry::Instance().Name(id));
            if (n == "snow_layer") {
                for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                    if (k == "height")
                        if (const int32_t* h = std::get_if<int32_t>(&v))
                            return *h == 0;
                return false;
            }
            return n == "water" || n == "flowing_water" || n == "lava" || n == "flowing_lava" || n == "short_grass" ||
                   n == "fern" || n == "deadbush" || n == "dead_bush" || n == "seagrass" || n == "fire" ||
                   n == "soul_fire" || n == "vine" || n == "glow_lichen" || n == "structure_void" ||
                   n == "crimson_roots" || n == "warped_roots" || n == "nether_sprouts" || n == "bubble_column" ||
                   n == "tall_grass" || n == "large_fern" || n == "hanging_roots" || n == "leaf_litter" ||
                   n.starts_with("light_block");
        }

        bool WaterSource(uint16_t id) {
            if (!waterlog::IsWater(id))
                return false;
            for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                if (k == "liquid_depth")
                    if (const int32_t* d = std::get_if<int32_t>(&v))
                        return *d == 0;
            return true;
        }
    }

    uint16_t Server::PlacementState(Session& by, uint16_t base, int face, double clickY, int& extraFace) {
        extraFace = -1;
        const std::string name = BlockRegistry::Instance().Name(base);
        const std::string bare = Bare(name);
        StateList states = BlockRegistry::Instance().States(base);
        const int look = Looking(by.Yaw());
        const int look3d = Looking3d(by.Yaw(), by.Pitch());
        const bool top = face == kDown || (face != kUp && clickY > 0.5);

        if (Has(states, "pillar_axis"))
            Put(states, "pillar_axis", std::string(face <= kUp ? "y" : face <= kSouth ? "z" : "x"));
        if (Has(states, "minecraft:cardinal_direction")) {
            // Most blocks turn their front to the player; doors, gates and campfires take the way the
            // player looks, anvils lie across it.
            int facing = Opposite(look);
            if (bare.contains("anvil"))
                facing = Clockwise(look);
            else if (bare.ends_with("door") || bare.ends_with("fence_gate") || bare.ends_with("campfire"))
                facing = look;
            Put(states, "minecraft:cardinal_direction", std::string(kFaceName[facing]));
        }
        if (Has(states, "direction")) {
            int32_t value = Legacy(Opposite(look));
            if (bare.ends_with("trapdoor"))
                value = 5 - Opposite(look); // north 3, south 2, west 1, east 0
            else if (bare == "bed" || bare == "straw_bed" || bare == "decorated_pot")
                value = Legacy(look);
            else if ((bare == "cocoa" || bare == "tripwire_hook") && face >= kNorth)
                value = Legacy(bare == "cocoa" ? Opposite(face) : face);
            Put(states, "direction", value);
            extraFace = look;
        }
        if (Has(states, "weirdo_direction"))
            Put(states, "weirdo_direction", Weirdo(look));
        if (Has(states, "upside_down_bit"))
            Put(states, "upside_down_bit", top);
        if (Has(states, "minecraft:vertical_half"))
            Put(states, "minecraft:vertical_half", std::string(top ? "top" : "bottom"));

        const auto facingFor = [&]() -> int {
            if (bare == "observer")
                return look3d;
            if (bare == "hopper")
                return face <= kUp ? kDown : Opposite(face);
            if (bare == "ladder" || bare.ends_with("frame") || bare.ends_with("lightning_rod") || bare == "end_rod" ||
                bare.contains("amethyst") || bare.ends_with("wall_sign") || bare.ends_with("wall_banner") ||
                bare.ends_with("button") || bare.ends_with("_head") || bare.ends_with("_skull"))
                return face;
            if (bare.ends_with("glazed_terracotta") || bare.ends_with("hanging_sign"))
                return Opposite(look);
            return Opposite(look3d);
        };
        if (Has(states, "facing_direction") && !bare.ends_with("_stem")) {
            int f = facingFor();
            if (bare == "end_rod" && f >= kNorth)
                f = Opposite(f); // the end rod numbers its sideways faces the other way round
            Put(states, "facing_direction", static_cast<int32_t>(f));
        }
        if (Has(states, "minecraft:facing_direction"))
            Put(states, "minecraft:facing_direction", std::string(kFaceName[facingFor()]));
        if (Has(states, "minecraft:block_face"))
            Put(states, "minecraft:block_face", std::string(kFaceName[face]));
        if (Has(states, "torch_facing_direction")) {
            static const char* const kTorch[6] = {"top", "top", "south", "north", "east", "west"};
            Put(states, "torch_facing_direction", std::string(kTorch[face]));
        }
        if (Has(states, "ground_sign_direction"))
            Put(states, "ground_sign_direction",
                static_cast<int32_t>(static_cast<int>(std::floor((by.Yaw() + 180.0f) * 16.0f / 360.0f + 0.5f)) & 15));
        if (Has(states, "hanging"))
            Put(states, "hanging", face == kDown);
        if (Has(states, "attachment") && bare == "bell")
            Put(states, "attachment", std::string(face == kUp ? "standing" : face == kDown ? "hanging" : "side"));
        return Valid(name, std::move(states), base);
    }

    bool Server::PlayerPlace(Session& by, int x, int y, int z, int face, double clickX, double clickY, double clickZ) {
        (void)clickX;
        (void)clickZ;
        if (face < 0 || face > 5)
            return false;
        const int dim = by.DimensionId();
        ChunkManager& world = World(dim);
        const DimensionRange range = RangeOf(dim);
        int tx = x + kOffset[face][0], ty = y + kOffset[face][1], tz = z + kOffset[face][2];
        const auto reject = [&] {
            by.CorrectBlock(x, y, z);
            by.CorrectBlock(tx, ty, tz);
            by.CorrectBlock(tx, ty + 1, tz);
            by.SendSlot(by.Inventory().selected);
            return false;
        };
        const ItemStack& held = by.Inventory().Held();
        const CatalogItem* item = held.Empty() ? nullptr : BlockCatalog::Instance().ItemById(held.networkId);
        if (item && UseItemOn(by, dim, x, y, z, face, *item))
            return true;
        if (!item || !item->block || !world.Peek({x >> 4, z >> 4}))
            return reject();
        uint16_t block = BlockCatalog::Instance().RegistryId(item->blockHash);
        const std::string itemBlock = BlockRegistry::Instance().Name(block);

        // Clicking a block that is itself replaceable (grass, snow) places into it.
        const uint16_t clicked = world.GetBlock(x, y, z);
        if (clicked != BlockRegistry::kAir && Replaceable(clicked) && !waterlog::IsWater(clicked)) {
            tx = x;
            ty = y;
            tz = z;
        }

        // A slab onto a matching slab makes the double slab.
        const auto doubleOf = [](const std::string& slab) {
            std::string d = slab;
            if (const size_t at = d.rfind("_slab"); at != std::string::npos)
                d.replace(at, 5, "_double_slab");
            return d;
        };
        if (itemBlock.ends_with("_slab")) {
            const std::string half = TextState(clicked, "minecraft:vertical_half");
            if (BlockRegistry::Instance().Name(clicked) == itemBlock &&
                ((half == "bottom" && face == kUp) || (half == "top" && face == kDown))) {
                tx = x;
                ty = y;
                tz = z;
                const uint16_t full = Named(doubleOf(itemBlock));
                if (full != BlockRegistry::kAir && by.InReach(tx, ty, tz)) {
                    if (plugins->AnyoneListens(abi::Event::BlockPlace) &&
                        plugins->Dispatch<abi::Event::BlockPlace>({by.Id(), tx, ty, tz, item->name}))
                        return reject();
                    ApplyBlock(dim, tx, ty, tz, full);
                    if (!by.Creative()) {
                        by.Inventory().TakeHeld(1);
                        by.SendSlot(by.Inventory().selected);
                    }
                    return true;
                }
            }
        }

        if (ty < range.minY || ty > range.MaxY() || !by.InReach(tx, ty, tz) || !world.Peek({tx >> 4, tz >> 4}))
            return reject();
        const uint16_t there = world.GetBlock(tx, ty, tz);
        if (!Replaceable(there)) {
            // The other half of a slab already in the space.
            const std::string half = TextState(there, "minecraft:vertical_half");
            if (itemBlock.ends_with("_slab") && BlockRegistry::Instance().Name(there) == itemBlock && !half.empty()) {
                const uint16_t full = Named(doubleOf(itemBlock));
                if (full == BlockRegistry::kAir)
                    return reject();
                ApplyBlock(dim, tx, ty, tz, full);
                if (!by.Creative()) {
                    by.Inventory().TakeHeld(1);
                    by.SendSlot(by.Inventory().selected);
                }
                return true;
            }
            return reject();
        }

        // Signs and banners placed on a wall become their wall form.
        if (face >= kNorth) {
            std::string wall;
            const std::string bare = Bare(itemBlock);
            if (bare == "standing_sign")
                wall = "minecraft:wall_sign";
            else if (bare.ends_with("_standing_sign"))
                wall = "minecraft:" + bare.substr(0, bare.size() - 14) + "_wall_sign";
            else if (bare == "standing_banner")
                wall = "minecraft:wall_banner";
            if (!wall.empty())
                if (const uint16_t w = Named(wall); w != BlockRegistry::kAir)
                    block = w;
        } else if (face == kDown && (itemBlock.ends_with("_sign") || itemBlock.ends_with("banner") ||
                                     itemBlock.ends_with("torch") || itemBlock == "minecraft:ladder")) {
            return reject();
        }
        if (face == kUp && Bare(itemBlock) == "ladder")
            return reject();

        int extraFace = -1;
        block = PlacementState(by, block, face, clickY, extraFace);

        // A block with a collision box cannot go where a player stands.
        if (BlockSolid(block))
            for (Session* s : PlayersIn(dim)) {
                if (s->Spectator() || s->Dead())
                    continue;
                const double feet = s->Y() - 1.62;
                if (s->X() + 0.3 > tx && s->X() - 0.3 < tx + 1 && s->Z() + 0.3 > tz && s->Z() - 0.3 < tz + 1 &&
                    feet + 1.8 > ty && feet < ty + 1)
                    return reject();
            }

        // The second block of doors, tall plants and beds.
        int px = tx, py = ty, pz = tz;
        uint16_t partner = BlockRegistry::kAir;
        {
            StateList states = BlockRegistry::Instance().States(block);
            const std::string name = BlockRegistry::Instance().Name(block);
            if (Has(states, "upper_block_bit")) {
                py = ty + 1;
                Put(states, "upper_block_bit", true);
                partner = Valid(name, std::move(states), BlockRegistry::kAir);
            } else if (Has(states, "head_piece_bit") && extraFace >= 0) {
                px = tx + kOffset[extraFace][0];
                pz = tz + kOffset[extraFace][2];
                Put(states, "head_piece_bit", true);
                partner = Valid(name, std::move(states), BlockRegistry::kAir);
            }
            if (partner != BlockRegistry::kAir) {
                if (py > range.MaxY() || !world.Peek({px >> 4, pz >> 4}) || !Replaceable(world.GetBlock(px, py, pz)))
                    return reject();
            }
        }

        if (plugins->AnyoneListens(abi::Event::BlockPlace) &&
            plugins->Dispatch<abi::Event::BlockPlace>({by.Id(), tx, ty, tz, item->name}))
            return reject();

        // Water stays around a block that can hold it; anything else replaces the water.
        const bool keepWater = waterlog::Holds(block) && WaterSource(there);
        ApplyBlock(dim, tx, ty, tz, block);
        if (keepWater)
            SetLiquidAt(dim, tx, ty, tz, waterlog::Water());
        if (partner != BlockRegistry::kAir) {
            const bool partnerWet = waterlog::Holds(partner) && WaterSource(world.GetBlock(px, py, pz));
            ApplyBlock(dim, px, py, pz, partner);
            if (partnerWet)
                SetLiquidAt(dim, px, py, pz, waterlog::Water());
        }
        if (!by.Creative()) {
            by.Inventory().TakeHeld(1);
            by.SendSlot(by.Inventory().selected);
        }
        return true;
    }

    void Server::SetLiquidAt(int dimension, int x, int y, int z, uint16_t liquid) {
        ChunkManager& world = World(dimension);
        if (!world.SetLiquid(x, y, z, liquid))
            return;
        const int32_t hash = BlockRegistry::Instance().Hash(liquid);
        for (Session* s : PlayersIn(dimension))
            if (s->HasSent({x >> 4, z >> 4}))
                s->SendBlockUpdate(x, y, z, hash, 1);
    }

    // Breaking one half of a door, tall plant or bed takes the other half with it.
    void Server::BreakPartner(int dimension, int x, int y, int z, uint16_t broken) {
        ChunkManager& world = World(dimension);
        const std::string name = BlockRegistry::Instance().Name(broken);
        const StateList states = BlockRegistry::Instance().States(broken);
        if (Has(states, "upper_block_bit")) {
            const int oy = BoolState(broken, "upper_block_bit") ? y - 1 : y + 1;
            if (BlockRegistry::Instance().Name(world.GetBlock(x, oy, z)) == name)
                ApplyBlock(dimension, x, oy, z, BlockRegistry::kAir);
            return;
        }
        if (Has(states, "head_piece_bit") && Has(states, "direction")) {
            int32_t dir = 0;
            for (const auto& [k, v] : states)
                if (k == "direction")
                    if (const int32_t* d = std::get_if<int32_t>(&v))
                        dir = *d;
            static constexpr int kDir[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}}; // south, west, north, east
            const int sign = BoolState(broken, "head_piece_bit") ? -1 : 1;
            const int ox = x + sign * kDir[dir & 3][0], oz = z + sign * kDir[dir & 3][1];
            if (world.Peek({ox >> 4, oz >> 4}) && BlockRegistry::Instance().Name(world.GetBlock(ox, y, oz)) == name)
                ApplyBlock(dimension, ox, y, oz, BlockRegistry::kAir);
        }
    }
}
