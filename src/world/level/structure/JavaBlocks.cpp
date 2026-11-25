// Template block states to Bedrock blocks. The rules are written out by block family: names that
// differ, then each property in the Bedrock form (directions as Bedrock numbers them, bits as 0/1).
#include "world/level/structure/JavaBlocks.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <unordered_map>

namespace perlite::structure {
    namespace {
        using Props = std::map<std::string, std::string>;

        std::string Strip(std::string_view s) {
            if (s.starts_with("minecraft:"))
                s.remove_prefix(10);
            return std::string(s);
        }

        constexpr std::array<const char*, 4> kHorizontal = {"north", "east", "south", "west"};
        int HorizontalIndex(std::string_view d) {
            for (int i = 0; i < 4; i++)
                if (d == kHorizontal[static_cast<size_t>(i)])
                    return i;
            return -1;
        }
        std::string RotateDir(std::string_view d, int turns) {
            const int i = HorizontalIndex(d);
            return i < 0 ? std::string(d) : kHorizontal[static_cast<size_t>((i + turns) & 3)];
        }

        // facing_direction: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
        int FacingDirection(std::string_view d) {
            if (d == "down")
                return 0;
            if (d == "up")
                return 1;
            if (d == "north")
                return 2;
            if (d == "south")
                return 3;
            if (d == "west")
                return 4;
            if (d == "east")
                return 5;
            return 2;
        }
        // "direction" of beds, bells, looms, grindstones and pots: 0 south, 1 west, 2 north, 3 east.
        int LegacyDirection(std::string_view d) {
            if (d == "south")
                return 0;
            if (d == "west")
                return 1;
            if (d == "north")
                return 2;
            return 3;
        }
        // Stairs (weirdo_direction) and trapdoors (direction): 0 east, 1 west, 2 south, 3 north.
        int EastWestSouthNorth(std::string_view d) {
            if (d == "east")
                return 0;
            if (d == "west")
                return 1;
            if (d == "south")
                return 2;
            return 3;
        }
        std::string Opposite(std::string_view d) {
            if (d == "north")
                return "south";
            if (d == "south")
                return "north";
            if (d == "east")
                return "west";
            if (d == "west")
                return "east";
            if (d == "up")
                return "down";
            return "up";
        }
        std::string Bit(bool b) { return b ? "1" : "0"; }
        bool True(const JavaState& s, std::string_view key) { return s.Get(key) == "true"; }

        const char* ColorName(std::string_view legacy) {
            if (legacy == "silver")
                return "light_gray";
            static const char* const kColors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                                  "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};
            for (const char* c : kColors)
                if (legacy == c)
                    return c;
            return "white";
        }

        bool EndsWith(const std::string& s, std::string_view t) { return s.size() >= t.size() && s.ends_with(t); }

        // Java name to Bedrock name, for the names that differ (after the legacy variants are folded in).
        std::string RenameBlock(const std::string& n) {
            static const std::unordered_map<std::string, std::string> kRename = {
                {"grass", "short_grass"}, {"dirt_path", "grass_path"}, {"tripwire", "trip_wire"},
                {"waxed_copper_block", "waxed_copper"}, {"cave_air", "air"}, {"void_air", "air"},
                {"spawner", "mob_spawner"}, {"cobweb", "web"}, {"lily_pad", "waterlily"}, {"terracotta", "hardened_clay"},
                {"bricks", "brick_block"}, {"melon", "melon_block"}, {"magma_block", "magma"}, {"snow_block", "snow"},
                {"note_block", "noteblock"}, {"oak_door", "wooden_door"}, {"oak_fence_gate", "fence_gate"},
                {"oak_trapdoor", "trapdoor"}, {"oak_pressure_plate", "wooden_pressure_plate"}, {"oak_button", "wooden_button"},
                {"oak_sign", "standing_sign"}, {"oak_wall_sign", "wall_sign"}, {"dark_oak_sign", "darkoak_standing_sign"},
                {"dark_oak_wall_sign", "darkoak_wall_sign"}, {"chain", "iron_chain"}, {"nether_bricks", "nether_brick"},
                {"red_nether_bricks", "red_nether_brick"}, {"end_stone_bricks", "end_bricks"},
                {"end_stone_brick_stairs", "end_brick_stairs"}, {"cobblestone_stairs", "stone_stairs"},
                {"stone_stairs", "normal_stone_stairs"}, {"prismarine_brick_stairs", "prismarine_bricks_stairs"},
                {"light_gray_glazed_terracotta", "silver_glazed_terracotta"}, {"jack_o_lantern", "lit_pumpkin"},
                {"slime_block", "slime"}, {"sugar_cane", "reeds"}, {"powered_rail", "golden_rail"}, {"piston_head", "piston_arm_collision"},
                {"moving_piston", "moving_block"}, {"rose_bush", "rose_bush"}, {"dead_bush", "deadbush"}, {"tall_seagrass", "seagrass"},
                {"skeleton_wall_skull", "skeleton_skull"}, {"wither_skeleton_wall_skull", "wither_skeleton_skull"}, {"skull", "skeleton_skull"},
                {"zombie_head", "zombie_head"}, {"stone_slab", "normal_stone_slab"}, {"quartz_pillar", "quartz_pillar"},
                {"smooth_stone", "smooth_stone"}, {"mob_spawner", "mob_spawner"}, {"water_cauldron", "cauldron"},
                {"lava_cauldron", "cauldron"}, {"structure_void", "structure_void"}, {"wall_torch", "torch"}, {"wall_sign", "wall_sign"},
                {"soul_wall_torch", "soul_torch"}, {"redstone_wall_torch", "redstone_torch"}, {"wooden_door", "wooden_door"},
                {"fence", "oak_fence"}, {"leaves2", "dark_oak_leaves"}, {"log2", "dark_oak_log"}, {"web", "web"},
                {"waterlily", "waterlily"}, {"grass_path", "grass_path"}, {"melon_block", "melon_block"}, {"snow", "snow_layer"},
                {"attached_melon_stem", "melon_stem"}, {"attached_pumpkin_stem", "pumpkin_stem"}, {"smooth_quartz", "smooth_quartz"},
                {"iron_bars", "iron_bars"}, {"stonebrick", "stone_bricks"}, {"monster_egg", "infested_stone"}, {"planks", "oak_planks"},
                {"carrots", "carrots"}, {"potatoes", "potatoes"}, {"beetroots", "beetroot"}, {"wheat", "wheat"},
                {"rail", "rail"}, {"comparator", "unpowered_comparator"}, {"repeater", "unpowered_repeater"}};
            if (const auto it = kRename.find(n); it != kRename.end())
                return it->second;
            // Banners and signs of every colour and wood go to the common blocks.
            if (EndsWith(n, "_wall_banner"))
                return "wall_banner";
            if (EndsWith(n, "_banner"))
                return "standing_banner";
            if (EndsWith(n, "_wall_sign") && !n.contains("hanging"))
                return n;
            if (EndsWith(n, "_sign") && !n.contains("hanging") && !n.contains("standing") && !n.contains("wall"))
                return n.substr(0, n.size() - 5) + "_standing_sign";
            if (EndsWith(n, "_bed"))
                return "bed";
            if (n.starts_with("potted_"))
                return "flower_pot";
            if (EndsWith(n, "_slab")) {
                static const std::unordered_map<std::string, std::string> kSlabs = {
                    {"stone_brick_slab", "stone_brick_slab"}, {"end_stone_brick_slab", "end_stone_brick_slab"},
                    {"prismarine_brick_slab", "prismarine_brick_slab"}, {"nether_brick_slab", "nether_brick_slab"}};
                if (const auto it = kSlabs.find(n); it != kSlabs.end())
                    return it->second;
            }
            return n;
        }

        // Folds the old variant/colour properties into the name ("planks[variant=birch]" -> "birch_planks").
        void FoldLegacy(JavaState& s, std::string& n) {
            const std::string variant = s.Get("variant"), color = s.Get("color"), type = s.Get("type");
            if (n == "planks" || n == "double_wooden_slab" || n == "wooden_slab") {
                const std::string wood = variant.empty() ? "oak" : variant;
                n = n == "planks" ? wood + "_planks" : n == "wooden_slab" ? wood + "_slab" : wood + "_double_slab";
                if (n.starts_with("wooden_slab") || s.Get("half") == "top")
                    s.Set("type", "top");
            } else if (n == "log2" || n == "log") {
                n = (variant.empty() ? std::string(n == "log2" ? "acacia" : "oak") : variant) + "_log";
            } else if (n == "leaves2" || n == "leaves") {
                n = (variant.empty() ? std::string(n == "leaves2" ? "acacia" : "oak") : variant) + "_leaves";
                if (s.Get("decayable") == "false")
                    s.Set("persistent", "true");
            } else if (n == "sapling") {
                n = (type.empty() ? std::string("oak") : type) + "_sapling";
            } else if (n == "wool" || n == "carpet" || n == "stained_glass" || n == "stained_glass_pane" || n == "concrete" ||
                       n == "stained_hardened_clay") {
                const std::string c = ColorName(color);
                n = n == "stained_hardened_clay" ? c + "_terracotta" : c + "_" + n;
            } else if (n == "stonebrick") {
                n = variant == "mossy_stonebrick" ? "mossy_stone_bricks"
                    : variant == "cracked_stonebrick" ? "cracked_stone_bricks"
                    : variant == "chiseled_stonebrick" ? "chiseled_stone_bricks"
                                                       : "stone_bricks";
            } else if (n == "monster_egg") {
                n = variant == "cobblestone" ? "infested_cobblestone"
                    : variant == "stone_brick" ? "infested_stone_bricks"
                    : variant == "mossy_brick" ? "infested_mossy_stone_bricks"
                    : variant == "cracked_brick" ? "infested_cracked_stone_bricks"
                    : variant == "chiseled_brick" ? "infested_chiseled_stone_bricks"
                                                  : "infested_stone";
            } else if (n == "stone" && !variant.empty()) {
                n = variant == "smooth_andesite" ? "polished_andesite"
                    : variant == "smooth_diorite" ? "polished_diorite"
                    : variant == "smooth_granite" ? "polished_granite"
                                                  : variant;
            } else if (n == "dirt" && variant == "coarse_dirt") {
                n = "coarse_dirt";
            } else if (n == "sand" && variant == "red_sand") {
                n = "red_sand";
            } else if (n == "coral_block") {
                n = variant.starts_with("dead") ? "dead_tube_coral_block" : "tube_coral_block";
            } else if (n == "double_stone_slab") {
                n = variant == "cobblestone" ? "cobblestone_double_slab"
                    : variant == "sandstone" ? "sandstone_double_slab"
                    : variant == "brick" ? "brick_double_slab"
                    : variant == "stone_brick" ? "stone_brick_double_slab"
                                               : "smooth_stone_double_slab";
            } else if (n == "stone_slab" && !variant.empty()) {
                n = variant == "cobblestone" ? "cobblestone_slab"
                    : variant == "sandstone" ? "sandstone_slab"
                    : variant == "brick" ? "brick_slab"
                    : variant == "stone_brick" ? "stone_brick_slab"
                                               : "smooth_stone_slab";
                if (s.Get("half") == "top")
                    s.Set("type", "top");
            } else if (n == "purpur_slab" && s.Get("half") == "top") {
                s.Set("type", "top");
            }
        }

        // Bedrock properties for the state; `n` is the final Bedrock name (may be changed for lit,
        // powered and double variants).
        Props Properties(const JavaState& s, std::string& n) {
            Props p;
            const std::string facing = s.Get("facing"), half = s.Get("half"), axis = s.Get("axis");
            // Double slabs are their own blocks.
            if (EndsWith(n, "_slab")) {
                if (s.Get("type") == "double")
                    n = n.substr(0, n.size() - 5) + "_double_slab";
                else
                    p["minecraft:vertical_half"] = s.Get("type") == "top" ? "top" : "bottom";
                return p;
            }
            if (EndsWith(n, "_stairs")) {
                p["weirdo_direction"] = std::to_string(EastWestSouthNorth(facing));
                p["upside_down_bit"] = Bit(half == "top");
                return p;
            }
            if (!axis.empty()) {
                p["pillar_axis"] = axis;
                return p;
            }
            if (EndsWith(n, "_door") || n == "wooden_door") {
                // A Bedrock door's cardinal direction is a quarter turn clockwise of the way it faces.
                static const std::unordered_map<std::string, std::string> kClockwise = {
                    {"north", "east"}, {"east", "south"}, {"south", "west"}, {"west", "north"}};
                const auto turned = kClockwise.find(facing.empty() ? "north" : facing);
                p["minecraft:cardinal_direction"] = turned->second;
                p["upper_block_bit"] = Bit(half == "upper");
                p["door_hinge_bit"] = Bit(s.Get("hinge") == "right");
                p["open_bit"] = Bit(True(s, "open"));
                return p;
            }
            if (EndsWith(n, "trapdoor")) {
                p["direction"] = std::to_string(EastWestSouthNorth(facing));
                p["upside_down_bit"] = Bit(half == "top");
                p["open_bit"] = Bit(True(s, "open"));
                return p;
            }
            if (EndsWith(n, "fence_gate")) {
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                p["open_bit"] = Bit(True(s, "open"));
                p["in_wall_bit"] = Bit(True(s, "in_wall"));
                return p;
            }
            if (n == "bed") {
                p["direction"] = std::to_string(LegacyDirection(facing));
                p["head_piece_bit"] = Bit(s.Get("part") == "head");
                p["occupied_bit"] = Bit(True(s, "occupied"));
                return p;
            }
            if (EndsWith(n, "_wall") && !n.contains("sign") && !n.contains("banner")) {
                for (const char* side : kHorizontal) {
                    std::string v = s.Get(side, "none");
                    v = v == "low" || v == "true" ? "short" : v == "tall" ? "tall" : "none";
                    p[std::string("wall_connection_type_") + side] = v;
                }
                p["wall_post_bit"] = Bit(s.Get("up", "true") == "true");
                return p;
            }
            if (EndsWith(n, "fence") || EndsWith(n, "pane") || n == "iron_bars" || n == "glass_pane") {
                for (const char* side : kHorizontal)
                    p[std::string("minecraft:connection_") + side] = Bit(True(s, side));
                return p;
            }
            if (n == "torch" || n == "soul_torch" || n == "redstone_torch" || n == "copper_torch") {
                if (n == "redstone_torch" && s.Get("lit") == "false")
                    n = "unlit_redstone_torch";
                // Wall torches name the side they lean towards; Bedrock names the block they hang on.
                p["torch_facing_direction"] = facing.empty() || facing == "up" ? "top" : Opposite(facing);
                return p;
            }
            if (n == "ladder" || n == "wall_sign" || EndsWith(n, "_wall_sign") || n == "wall_banner" || EndsWith(n, "glazed_terracotta") ||
                n == "barrel" || n == "end_rod" || n == "chiseled_bookshelf" || n == "observer" || n == "dispenser" ||
                n == "dropper" || n == "hopper" || n == "lightning_rod" || n == "amethyst_cluster" || n == "piston" ||
                n == "sticky_piston" || n == "piston_arm_collision" || n == "sticky_piston_arm_collision" || n == "jigsaw") {
                if (n == "piston_arm_collision" && s.Get("type") == "sticky")
                    n = "sticky_piston_arm_collision";
                p["facing_direction"] = std::to_string(FacingDirection(facing.empty() ? "north" : facing));
                if (n == "barrel")
                    p["open_bit"] = Bit(True(s, "open"));
                return p;
            }
            if (n == "skeleton_skull" || n == "wither_skeleton_skull" || EndsWith(n, "_head")) {
                p["facing_direction"] = facing.empty() ? "1" : std::to_string(FacingDirection(facing));
                return p;
            }
            if (EndsWith(n, "_button")) {
                const std::string face = s.Get("face");
                p["facing_direction"] = face == "floor" ? "1" : face == "ceiling" ? "0" : std::to_string(FacingDirection(facing));
                p["button_pressed_bit"] = Bit(True(s, "powered"));
                return p;
            }
            if (n == "lever") {
                const std::string face = s.Get("face");
                const bool ns = facing == "north" || facing == "south";
                p["lever_direction"] = face == "floor"     ? (ns ? "up_north_south" : "up_east_west")
                                       : face == "ceiling" ? (ns ? "down_north_south" : "down_east_west")
                                                           : facing;
                p["open_bit"] = Bit(True(s, "powered"));
                return p;
            }
            if (n == "bell" || n == "grindstone") {
                const std::string a = n == "bell" ? s.Get("attachment") : s.Get("face");
                p["attachment"] = a == "floor" ? "standing" : a == "ceiling" ? "hanging" : a == "double_wall" ? "multiple" : "side";
                p["direction"] = std::to_string(LegacyDirection(facing));
                return p;
            }
            if (n == "loom" || n == "decorated_pot") {
                p["direction"] = std::to_string(LegacyDirection(facing));
                return p;
            }
            if (n == "furnace" || n == "smoker" || n == "blast_furnace") {
                if (True(s, "lit"))
                    n = "lit_" + n;
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                return p;
            }
            if (n == "campfire" || n == "soul_campfire") {
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                p["extinguished"] = Bit(s.Get("lit") == "false");
                return p;
            }
            if (n == "trip_wire") {
                p["attached_bit"] = Bit(True(s, "attached"));
                p["disarmed_bit"] = Bit(True(s, "disarmed"));
                p["powered_bit"] = Bit(True(s, "powered"));
                p["suspended_bit"] = Bit(true);
                for (const char* side : {"north", "east", "south", "west"})
                    p[std::string("minecraft:connection_") + side] = Bit(True(s, side));
                return p;
            }
            if (n == "unpowered_repeater" || n == "unpowered_comparator") {
                if (True(s, "powered"))
                    n = n == "unpowered_repeater" ? "powered_repeater" : "powered_comparator";
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                if (n.contains("repeater"))
                    p["repeater_delay"] = std::to_string(std::max(0, std::atoi(s.Get("delay", "1").c_str()) - 1));
                else
                    p["output_subtract_bit"] = Bit(s.Get("mode") == "subtract");
                return p;
            }
            if (n == "chest" || n == "trapped_chest" || n == "ender_chest" || n == "lectern" || n == "anvil" || n == "chipped_anvil" ||
                n == "damaged_anvil" || n == "carved_pumpkin" || n == "lit_pumpkin" || n == "pumpkin" || n == "end_portal_frame" ||
                n == "stonecutter_block" || n == "beehive" || n == "bee_nest" || n == "crafter" || n == "vault" || n == "trial_spawner") {
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                return p;
            }
            if (n == "stonecutter") {
                n = "stonecutter_block";
                p["minecraft:cardinal_direction"] = facing.empty() ? "north" : facing;
                return p;
            }
            if (n == "standing_sign" || EndsWith(n, "_standing_sign") || n == "standing_banner") {
                p["ground_sign_direction"] = s.Get("rotation", "0");
                return p;
            }
            if (n == "lantern" || n == "soul_lantern") {
                p["hanging"] = Bit(True(s, "hanging"));
                return p;
            }
            if (n == "water" || n == "lava") {
                const int level = std::atoi(s.Get("level", "0").c_str());
                if (level != 0)
                    n = "flowing_" + n;
                p["liquid_depth"] = std::to_string(level);
                return p;
            }
            if (n == "wheat" || n == "carrots" || n == "potatoes" || n == "beetroot") {
                p["growth"] = s.Get("age", "0");
                return p;
            }
            if (n == "melon_stem" || n == "pumpkin_stem") {
                const bool attached = s.name.contains("attached");
                p["growth"] = attached ? "7" : s.Get("age", "0");
                p["facing_direction"] = attached ? std::to_string(FacingDirection(facing)) : "1";
                return p;
            }
            if (n == "cactus" || n == "nether_wart" || n == "fire" || n == "reeds" || n == "sweet_berry_bush") {
                p["age"] = s.Get("age", "0");
                return p;
            }
            if (n == "farmland") {
                p["moisturized_amount"] = s.Get("moisture", "0");
                return p;
            }
            if (n == "snow_layer") {
                p["height"] = std::to_string(std::max(0, std::atoi(s.Get("layers", "1").c_str()) - 1));
                return p;
            }
            if (EndsWith(n, "_leaves")) {
                p["persistent_bit"] = Bit(True(s, "persistent"));
                return p;
            }
            if (EndsWith(n, "candle")) {
                p["candles"] = std::to_string(std::max(0, std::atoi(s.Get("candles", "1").c_str()) - 1));
                p["lit"] = Bit(True(s, "lit"));
                return p;
            }
            if (n == "sea_pickle") {
                p["cluster_count"] = std::to_string(std::max(0, std::atoi(s.Get("pickles", "1").c_str()) - 1));
                p["dead_bit"] = Bit(s.Get("waterlogged") == "false");
                return p;
            }
            if (n == "cauldron") {
                const int level = std::atoi(s.Get("level", "0").c_str());
                p["fill_level"] = std::to_string(s.name.contains("lava") ? 6 : level * 2);
                p["cauldron_liquid"] = s.name.contains("lava") ? "lava" : "water";
                return p;
            }
            if (n == "composter") {
                p["composter_fill_level"] = s.Get("level", "0");
                return p;
            }
            if (n == "brewing_stand") {
                p["brewing_stand_slot_a_bit"] = Bit(True(s, "has_bottle_0"));
                p["brewing_stand_slot_b_bit"] = Bit(True(s, "has_bottle_1"));
                p["brewing_stand_slot_c_bit"] = Bit(True(s, "has_bottle_2"));
                return p;
            }
            if (n == "redstone_wire") {
                p["redstone_signal"] = s.Get("power", "0");
                return p;
            }
            if (n == "redstone_lamp" && True(s, "lit")) {
                n = "lit_redstone_lamp";
                return p;
            }
            if (n == "rail" || n == "golden_rail" || n == "detector_rail" || n == "activator_rail") {
                static const std::unordered_map<std::string, int> kRails = {
                    {"north_south", 0}, {"east_west", 1}, {"ascending_east", 2}, {"ascending_west", 3}, {"ascending_north", 4},
                    {"ascending_south", 5}, {"south_east", 6}, {"south_west", 7}, {"north_west", 8}, {"north_east", 9}};
                const auto it = kRails.find(s.Get("shape", "north_south"));
                p["rail_direction"] = std::to_string(it == kRails.end() ? 0 : it->second);
                if (n != "rail")
                    p["rail_data_bit"] = Bit(True(s, "powered"));
                return p;
            }
            if (n == "vine") {
                const int bits = (True(s, "south") ? 1 : 0) | (True(s, "west") ? 2 : 0) | (True(s, "north") ? 4 : 0) | (True(s, "east") ? 8 : 0);
                p["vine_direction_bits"] = std::to_string(bits);
                return p;
            }
            if (n == "tall_grass" || n == "large_fern" || n == "sunflower" || n == "lilac" || n == "rose_bush" || n == "peony" ||
                n == "pitcher_plant") {
                p["upper_block_bit"] = Bit(half == "upper");
                return p;
            }
            if (n == "tnt") {
                p["explode_bit"] = Bit(True(s, "unstable"));
                return p;
            }
            if (n == "sculk_sensor") {
                const std::string phase = s.Get("sculk_sensor_phase", "inactive");
                p["sculk_sensor_phase"] = phase == "active" ? "1" : phase == "cooldown" ? "2" : "0";
                return p;
            }
            if (n == "sculk_shrieker") {
                p["can_summon"] = Bit(True(s, "can_summon"));
                return p;
            }
            if (n == "respawn_anchor") {
                p["respawn_anchor_charge"] = s.Get("charges", "0");
                return p;
            }
            if (n == "suspicious_gravel" || n == "suspicious_sand") {
                p["brushed_progress"] = s.Get("dusted", "0");
                return p;
            }
            if (n == "structure_block") {
                p["structure_block_type"] = s.Get("mode", "data");
                return p;
            }
            if (EndsWith(n, "pressure_plate")) {
                p["redstone_signal"] = Bit(True(s, "powered"));
                return p;
            }
            if (n == "end_portal_frame") {
                p["end_portal_eye_bit"] = Bit(True(s, "eye"));
                return p;
            }
            return p;
        }

        std::string Translate(const JavaState& in) {
            JavaState s = in;
            std::string n = Strip(s.name);
            FoldLegacy(s, n);
            n = RenameBlock(n);
            Props p = Properties(s, n);
            if (n == "air" || n == "structure_void")
                return "minecraft:" + n;
            std::string text = "minecraft:" + n;
            if (!p.empty()) {
                text += '[';
                bool first = true;
                for (const auto& [k, v] : p) {
                    if (!first)
                        text += ',';
                    first = false;
                    text += k + '=' + v;
                }
                text += ']';
            }
            return text;
        }
    }

    std::string JavaState::Get(std::string_view key, std::string_view fallback) const {
        for (const auto& [k, v] : props)
            if (k == key)
                return v;
        return std::string(fallback);
    }

    void JavaState::Set(std::string_view key, std::string_view value) {
        for (auto& [k, v] : props)
            if (k == key) {
                v = value;
                return;
            }
        props.emplace_back(std::string(key), std::string(value));
    }

    std::string JavaState::Text() const {
        std::string out = name;
        if (!props.empty()) {
            std::vector<std::pair<std::string, std::string>> sorted = props;
            std::ranges::sort(sorted);
            out += '[';
            for (size_t i = 0; i < sorted.size(); i++) {
                if (i)
                    out += ',';
                out += sorted[i].first + '=' + sorted[i].second;
            }
            out += ']';
        }
        return out;
    }

    JavaState ParseState(std::string_view text) {
        JavaState s;
        const size_t open = text.find('[');
        s.name = std::string(text.substr(0, open));
        if (!s.name.contains(':'))
            s.name = "minecraft:" + s.name;
        if (open == std::string_view::npos)
            return s;
        std::string_view body = text.substr(open + 1);
        if (body.ends_with(']'))
            body.remove_suffix(1);
        while (!body.empty()) {
            const size_t comma = body.find(',');
            const std::string_view part = body.substr(0, comma);
            if (const size_t eq = part.find('='); eq != std::string_view::npos)
                s.props.emplace_back(std::string(part.substr(0, eq)), std::string(part.substr(eq + 1)));
            if (comma == std::string_view::npos)
                break;
            body.remove_prefix(comma + 1);
        }
        return s;
    }

    JavaState Rotated(const JavaState& in, int turns) {
        turns &= 3;
        if (turns == 0)
            return in;
        JavaState s = in;
        std::array<std::string, 4> sides;
        bool hasSides = false;
        for (int i = 0; i < 4; i++) {
            sides[static_cast<size_t>(i)] = in.Get(kHorizontal[static_cast<size_t>(i)]);
            hasSides = hasSides || !sides[static_cast<size_t>(i)].empty();
        }
        for (auto& [k, v] : s.props) {
            if (k == "facing" || k == "horizontal_facing")
                v = RotateDir(v, turns);
            else if (k == "axis" && (turns & 1) && (v == "x" || v == "z"))
                v = v == "x" ? "z" : "x";
            else if (k == "rotation")
                v = std::to_string((std::atoi(v.c_str()) + 4 * turns) & 15);
            else if (k == "orientation") {
                const size_t u = v.find('_');
                if (u != std::string::npos)
                    v = RotateDir(v.substr(0, u), turns) + "_" + RotateDir(v.substr(u + 1), turns);
            } else if (k == "shape" && (s.name.contains("rail"))) {
                if (v == "north_south" || v == "east_west") {
                    if (turns & 1)
                        v = v == "north_south" ? "east_west" : "north_south";
                } else if (v.starts_with("ascending_")) {
                    v = "ascending_" + RotateDir(v.substr(10), turns);
                } else if (const size_t u = v.find('_'); u != std::string::npos) {
                    std::string a = RotateDir(v.substr(0, u), turns), b = RotateDir(v.substr(u + 1), turns);
                    // Rails name the north/south side first.
                    if (a == "east" || a == "west")
                        std::swap(a, b);
                    v = a + "_" + b;
                }
            }
        }
        if (hasSides)
            for (int i = 0; i < 4; i++)
                if (!sides[static_cast<size_t>(i)].empty())
                    s.Set(kHorizontal[static_cast<size_t>((i + turns) & 3)], sides[static_cast<size_t>(i)]);
        return s;
    }

    std::string BedrockText(const JavaState& state) { return Translate(state); }

    uint16_t ToBedrock(const JavaState& state) {
        static std::mutex mutex;
        static std::unordered_map<std::string, uint16_t> cache;
        const std::string key = state.Text();
        {
            std::lock_guard lock(mutex);
            if (const auto it = cache.find(key); it != cache.end())
                return it->second;
        }
        const std::string text = Translate(state);
        const std::string name = text.substr(0, text.find('['));
        uint16_t id = BlockRegistry::kAir;
        if (name != "minecraft:air" && BlockCatalog::Instance().Knows(name))
            id = BlockCatalog::Instance().Resolve(text);
        std::lock_guard lock(mutex);
        cache[key] = id;
        return id;
    }
}
