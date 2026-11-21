#include "server/ServerData.hpp"

#include "protocol/NbtWriter.hpp"
#include "world/actor/mob/MobRegistry.hpp"

#include <set>
#include <unordered_map>

namespace perlite::serverdata {
    namespace {
        struct LegacyId {
            const char* name;
            int32_t id;
        };

        constexpr LegacyId kLegacyIds[] = {{"chicken", 10},
                                           {"cow", 11},
                                           {"pig", 12},
                                           {"sheep", 13},
                                           {"wolf", 14},
                                           {"villager", 15},
                                           {"mooshroom", 16},
                                           {"squid", 17},
                                           {"rabbit", 18},
                                           {"bat", 19},
                                           {"iron_golem", 20},
                                           {"snow_golem", 21},
                                           {"ocelot", 22},
                                           {"horse", 23},
                                           {"donkey", 24},
                                           {"mule", 25},
                                           {"skeleton_horse", 26},
                                           {"zombie_horse", 27},
                                           {"polar_bear", 28},
                                           {"llama", 29},
                                           {"parrot", 30},
                                           {"dolphin", 31},
                                           {"zombie", 32},
                                           {"creeper", 33},
                                           {"skeleton", 34},
                                           {"spider", 35},
                                           {"zombie_pigman", 36},
                                           {"slime", 37},
                                           {"enderman", 38},
                                           {"silverfish", 39},
                                           {"cave_spider", 40},
                                           {"ghast", 41},
                                           {"magma_cube", 42},
                                           {"blaze", 43},
                                           {"zombie_villager", 44},
                                           {"witch", 45},
                                           {"stray", 46},
                                           {"husk", 47},
                                           {"wither_skeleton", 48},
                                           {"guardian", 49},
                                           {"elder_guardian", 50},
                                           {"npc", 51},
                                           {"wither", 52},
                                           {"ender_dragon", 53},
                                           {"shulker", 54},
                                           {"endermite", 55},
                                           {"agent", 56},
                                           {"vindicator", 57},
                                           {"phantom", 58},
                                           {"armor_stand", 61},
                                           {"tripod_camera", 62},
                                           {"player", 63},
                                           {"item", 64},
                                           {"tnt", 65},
                                           {"falling_block", 66},
                                           {"xp_bottle", 68},
                                           {"xp_orb", 69},
                                           {"eye_of_ender_signal", 70},
                                           {"ender_crystal", 71},
                                           {"fireworks_rocket", 72},
                                           {"thrown_trident", 73},
                                           {"turtle", 74},
                                           {"cat", 75},
                                           {"shulker_bullet", 76},
                                           {"fishing_hook", 77},
                                           {"dragon_fireball", 79},
                                           {"arrow", 80},
                                           {"snowball", 81},
                                           {"egg", 82},
                                           {"painting", 83},
                                           {"minecart", 84},
                                           {"fireball", 85},
                                           {"splash_potion", 86},
                                           {"ender_pearl", 87},
                                           {"leash_knot", 88},
                                           {"wither_skull", 89},
                                           {"boat", 90},
                                           {"wither_skull_dangerous", 91},
                                           {"lightning_bolt", 93},
                                           {"small_fireball", 94},
                                           {"area_effect_cloud", 95},
                                           {"hopper_minecart", 96},
                                           {"tnt_minecart", 97},
                                           {"chest_minecart", 98},
                                           {"command_block_minecart", 100},
                                           {"lingering_potion", 101},
                                           {"llama_spit", 102},
                                           {"evocation_fang", 103},
                                           {"evocation_illager", 104},
                                           {"vex", 105},
                                           {"ice_bomb", 106},
                                           {"pufferfish", 108},
                                           {"salmon", 109},
                                           {"drowned", 110},
                                           {"tropicalfish", 111},
                                           {"cod", 112},
                                           {"panda", 113},
                                           {"pillager", 114},
                                           {"villager_v2", 115},
                                           {"zombie_villager_v2", 116},
                                           {"wandering_trader", 118},
                                           {"fox", 121},
                                           {"bee", 122},
                                           {"piglin", 123},
                                           {"hoglin", 124},
                                           {"strider", 125},
                                           {"zoglin", 126},
                                           {"piglin_brute", 127},
                                           {"goat", 128},
                                           {"glow_squid", 129},
                                           {"axolotl", 130},
                                           {"warden", 131},
                                           {"frog", 132},
                                           {"tadpole", 133},
                                           {"allay", 134},
                                           {"camel", 138},
                                           {"sniffer", 139},
                                           {"breeze", 140},
                                           {"breeze_wind_charge_projectile", 141},
                                           {"armadillo", 142},
                                           {"wind_charge_projectile", 143},
                                           {"bogged", 144},
                                           {"ominous_item_spawner", 145},
                                           {"creaking", 146},
                                           {"happy_ghast", 147},
                                           {"trader_llama", 157},
                                           {"chest_boat", 218}};

        constexpr const char* kNonMobs[] = {"player",
                                            "item",
                                            "tnt",
                                            "falling_block",
                                            "xp_bottle",
                                            "xp_orb",
                                            "eye_of_ender_signal",
                                            "ender_crystal",
                                            "fireworks_rocket",
                                            "thrown_trident",
                                            "shulker_bullet",
                                            "fishing_hook",
                                            "dragon_fireball",
                                            "arrow",
                                            "snowball",
                                            "egg",
                                            "painting",
                                            "minecart",
                                            "fireball",
                                            "splash_potion",
                                            "ender_pearl",
                                            "leash_knot",
                                            "wither_skull",
                                            "boat",
                                            "wither_skull_dangerous",
                                            "lightning_bolt",
                                            "small_fireball",
                                            "area_effect_cloud",
                                            "hopper_minecart",
                                            "tnt_minecart",
                                            "chest_minecart",
                                            "command_block_minecart",
                                            "lingering_potion",
                                            "llama_spit",
                                            "evocation_fang",
                                            "breeze_wind_charge_projectile",
                                            "wind_charge_projectile",
                                            "ominous_item_spawner",
                                            "chest_boat"};

        struct Biome {
            const char* name;
            float temperature;
            float downfall;
            bool rain;
            uint32_t water;
        };

        constexpr uint32_t kWater = 0x44AFF5;
        constexpr Biome kBiomes[] = {
            {"plains", 0.8f, 0.4f, true, kWater},
            {"sunflower_plains", 0.8f, 0.4f, true, kWater},
            {"desert", 2.0f, 0.0f, false, kWater},
            {"desert_mutated", 2.0f, 0.0f, false, kWater},
            {"swampland", 0.8f, 0.9f, true, 0x617B64},
            {"swampland_mutated", 0.8f, 0.9f, true, 0x617B64},
            {"mangrove_swamp", 0.8f, 0.9f, true, 0x3A7A6A},
            {"river", 0.5f, 0.5f, true, kWater},
            {"frozen_river", 0.0f, 0.5f, true, 0x185390},
            {"ocean", 0.5f, 0.5f, true, kWater},
            {"deep_ocean", 0.5f, 0.5f, true, kWater},
            {"warm_ocean", 0.8f, 0.5f, true, 0x43D5EE},
            {"lukewarm_ocean", 0.8f, 0.5f, true, 0x45ADF2},
            {"deep_lukewarm_ocean", 0.8f, 0.5f, true, 0x45ADF2},
            {"cold_ocean", 0.5f, 0.5f, true, 0x3D57D6},
            {"deep_cold_ocean", 0.5f, 0.5f, true, 0x3D57D6},
            {"frozen_ocean", 0.0f, 0.5f, true, 0x3938C9},
            {"deep_frozen_ocean", 0.0f, 0.5f, true, 0x3938C9},
            {"legacy_frozen_ocean", 0.0f, 0.5f, true, 0x3938C9},
            {"ice_plains", 0.0f, 0.5f, true, kWater},
            {"ice_plains_spikes", 0.0f, 1.0f, true, kWater},
            {"ice_mountains", 0.0f, 0.5f, true, kWater},
            {"forest", 0.7f, 0.8f, true, kWater},
            {"forest_hills", 0.7f, 0.8f, true, kWater},
            {"flower_forest", 0.7f, 0.8f, true, kWater},
            {"birch_forest", 0.6f, 0.6f, true, kWater},
            {"birch_forest_hills", 0.6f, 0.6f, true, kWater},
            {"birch_forest_mutated", 0.6f, 0.6f, true, kWater},
            {"birch_forest_hills_mutated", 0.6f, 0.6f, true, kWater},
            {"roofed_forest", 0.7f, 0.8f, true, kWater},
            {"roofed_forest_mutated", 0.7f, 0.8f, true, kWater},
            {"taiga", 0.25f, 0.8f, true, kWater},
            {"taiga_hills", 0.25f, 0.8f, true, kWater},
            {"taiga_mutated", 0.25f, 0.8f, true, kWater},
            {"cold_taiga", -0.5f, 0.4f, true, kWater},
            {"cold_taiga_hills", -0.5f, 0.4f, true, kWater},
            {"cold_taiga_mutated", -0.5f, 0.4f, true, kWater},
            {"mega_taiga", 0.3f, 0.8f, true, kWater},
            {"mega_taiga_hills", 0.3f, 0.8f, true, kWater},
            {"redwood_taiga_mutated", 0.25f, 0.8f, true, kWater},
            {"redwood_taiga_hills_mutated", 0.25f, 0.8f, true, kWater},
            {"extreme_hills", 0.2f, 0.3f, true, kWater},
            {"extreme_hills_edge", 0.2f, 0.3f, true, kWater},
            {"extreme_hills_mutated", 0.2f, 0.3f, true, kWater},
            {"extreme_hills_plus_trees", 0.2f, 0.3f, true, kWater},
            {"extreme_hills_plus_trees_mutated", 0.2f, 0.3f, true, kWater},
            {"savanna", 1.2f, 0.0f, false, kWater},
            {"savanna_plateau", 1.0f, 0.0f, false, kWater},
            {"savanna_mutated", 1.1f, 0.0f, false, kWater},
            {"savanna_plateau_mutated", 1.0f, 0.0f, false, kWater},
            {"jungle", 0.95f, 0.9f, true, kWater},
            {"jungle_hills", 0.95f, 0.9f, true, kWater},
            {"jungle_mutated", 0.95f, 0.9f, true, kWater},
            {"jungle_edge", 0.95f, 0.8f, true, kWater},
            {"jungle_edge_mutated", 0.95f, 0.8f, true, kWater},
            {"bamboo_jungle", 0.95f, 0.9f, true, kWater},
            {"bamboo_jungle_hills", 0.95f, 0.9f, true, kWater},
            {"mesa", 2.0f, 0.0f, false, kWater},
            {"mesa_plateau", 2.0f, 0.0f, false, kWater},
            {"mesa_plateau_stone", 2.0f, 0.0f, false, kWater},
            {"mesa_plateau_mutated", 2.0f, 0.0f, false, kWater},
            {"mesa_plateau_stone_mutated", 2.0f, 0.0f, false, kWater},
            {"mesa_bryce", 2.0f, 0.0f, false, kWater},
            {"mushroom_island", 0.9f, 1.0f, true, kWater},
            {"mushroom_island_shore", 0.9f, 1.0f, true, kWater},
            {"beach", 0.8f, 0.4f, true, kWater},
            {"stone_beach", 0.2f, 0.3f, true, kWater},
            {"cold_beach", 0.05f, 0.3f, true, kWater},
            {"meadow", 0.5f, 0.8f, true, kWater},
            {"grove", -0.2f, 0.8f, true, kWater},
            {"snowy_slopes", -0.3f, 0.9f, true, kWater},
            {"frozen_peaks", -0.7f, 0.9f, true, kWater},
            {"jagged_peaks", -0.7f, 0.9f, true, kWater},
            {"stony_peaks", 1.0f, 0.3f, true, kWater},
            {"lush_caves", 0.5f, 0.5f, true, kWater},
            {"dripstone_caves", 0.8f, 0.4f, true, kWater},
            {"deep_dark", 0.8f, 0.4f, true, kWater},
            {"cherry_grove", 0.5f, 0.8f, true, 0x5DB7EF},
            {"pale_garden", 0.7f, 0.8f, true, 0x76889D},
            {"hell", 2.0f, 0.0f, false, 0x905957},
            {"crimson_forest", 2.0f, 0.0f, false, 0x905957},
            {"warped_forest", 2.0f, 0.0f, false, 0x905957},
            {"soulsand_valley", 2.0f, 0.0f, false, 0x905957},
            {"basalt_deltas", 2.0f, 0.0f, false, 0x3F76E4},
            {"the_end", 0.5f, 0.5f, false, 0x62529E},
        };
    }

    int32_t LegacyEntityId(const std::string& identifier) {
        static const std::unordered_map<std::string, int32_t> table = [] {
            std::unordered_map<std::string, int32_t> t;
            for (const LegacyId& l : kLegacyIds)
                t[std::string("minecraft:") + l.name] = l.id;
            return t;
        }();
        const auto it = table.find(identifier);
        return it == table.end() ? 0 : it->second;
    }

    const std::vector<EntityType>& EntityTypes() {
        static const std::vector<EntityType> types = [] {
            std::vector<EntityType> out;
            std::set<std::string> seen;
            int32_t nextId = 300;
            for (const MobType& type : MobRegistry::Instance().All()) {
                EntityType e;
                e.id = type.def.id;
                e.legacyId = LegacyEntityId(e.id);
                if (e.legacyId == 0)
                    e.legacyId = nextId++;
                e.spawnEgg = type.category != MobCategory::Other && type.category != MobCategory::Boss;
                e.summonable = true;
                if (seen.insert(e.id).second)
                    out.push_back(std::move(e));
            }
            for (const char* name : kNonMobs) {
                EntityType e;
                e.id = std::string("minecraft:") + name;
                e.legacyId = LegacyEntityId(e.id);
                e.summonable = e.id != "minecraft:player";
                if (seen.insert(e.id).second)
                    out.push_back(std::move(e));
            }
            return out;
        }();
        return types;
    }

    const std::vector<uint8_t>& EntityIdentifiersNbt() {
        static const std::vector<uint8_t> bytes = [] {
            nbt::Writer w;
            w.BeginRoot();
            const auto& types = EntityTypes();
            w.BeginList("idlist", nbt::Compound, static_cast<int32_t>(types.size()));
            for (const EntityType& t : types) {
                w.PutString("bid", "");
                w.PutByte("hasspawnegg", t.spawnEgg ? 1 : 0);
                w.PutString("id", t.id);
                w.PutInt("rid", t.legacyId);
                w.PutByte("summonable", t.summonable ? 1 : 0);
                w.EndCompound();
            }
            w.EndCompound();
            return w.Bytes();
        }();
        return bytes;
    }

    const proto2225::BiomeDefinitionListPacket& BiomeDefinitions() {
        static const proto2225::BiomeDefinitionListPacket packet = [] {
            proto2225::BiomeDefinitionListPacket p;
            for (const Biome& b : kBiomes) {
                proto2225::BiomeDefinition d;
                d.nameIndex = static_cast<int16_t>(p.stringList.size());
                p.stringList.emplace_back(b.name);
                d.biomeId = 0xFFFF;
                d.temperature = b.temperature;
                d.downfall = b.downfall;
                d.snowFoliage = 0.0f;
                d.depth = 0.1f;
                d.scale = 0.2f;
                d.mapWaterColour = static_cast<int32_t>(0xA5000000u | b.water);
                d.rain = b.rain;
                p.biomeDefinitions.push_back(std::move(d));
            }
            return p;
        }();
        return packet;
    }
}
