#pragma once

#include "world/actor/ActorManager.hpp"
#include "server/Server.hpp"
#include "network/session/Session.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/loot/Loot.hpp"
#include "world/actor/mob/MobRegistry.hpp"
#include <algorithm>
#include <vector>
#include <random>
#include <unordered_set>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace perlite {

    namespace detail {
        constexpr double kPi = 3.14159265358979323846;

        struct Shape {
            bool solid = false;
            bool liquid = false;
        };

        inline Shape ShapeOf(uint16_t id) {
            static std::mutex mutex;
            static std::vector<int8_t> cache;
            {
                std::lock_guard lock(mutex);
                if (id < cache.size() && cache[id])
                    return {cache[id] == 2, cache[id] == 3};
            }
            const std::string n = BlockRegistry::Instance().Name(id);
            int8_t kind = 1;
            if (id == 0 || n.ends_with(":air"))
                kind = 1;
            else if (n.contains("water") || n.contains("lava"))
                kind = 3;
            else {
                static const char* const passable[] = {"grass", "fern", "flower", "sapling", "bush", "vine", "torch", "rail",
                                                       "button", "pressure_plate", "lever", "sign", "banner", "carpet",
                                                       "snow_layer", "seagrass", "kelp", "reeds", "deadbush", "mushroom",
                                                       "petals", "wildflowers", "leaf_litter", "tulip", "poppy", "dandelion",
                                                       "orchid", "allium", "bluet", "daisy", "lilac", "peony", "rose",
                                                       "sunflower", "lichen", "roots", "dripleaf", "wheat", "carrots",
                                                       "potatoes", "beetroot", "cocoa", "web", "portal", "fire", "light_block",
                                                       "redstone_wire", "tripwire", "lily", "cornflower", "eyeblossom", "pitcher",
                                                       "torchflower", "spore_blossom", "hanging", "structure_void"};
                bool pass = false;
                for (const char* p : passable)
                    if (n.contains(p) && !n.contains("block") && !n.contains("stem")) {
                        pass = true;
                        break;
                    }
                kind = pass ? 1 : 2;
            }
            std::lock_guard lock(mutex);
            if (cache.size() <= id)
                cache.resize(static_cast<size_t>(id) + 1, 0);
            cache[id] = kind;
            return {kind == 2, kind == 3};
        }

        inline uint8_t Angle(float degrees) {
            return static_cast<uint8_t>(static_cast<int>(std::floor(degrees / 360.0f * 256.0f)) & 255);
        }

        inline const std::unordered_map<std::string, std::string>& ItemAliases() {
            static const std::unordered_map<std::string, std::string> a = {
                {"muttonRaw", "mutton"}, {"dye", "ink_sac"}, {"skull", "wither_skeleton_skull"},
                {"fish", "cod"}, {"rabbit_hide", "rabbit_hide"}, {"appleEnchanted", "enchanted_golden_apple"}};
            return a;
        }

        inline float BlastResistance(uint16_t id) {
            static std::vector<float> memo;
            static std::mutex memoMutex;
            std::lock_guard lock(memoMutex);
            if (id >= memo.size())
                memo.resize(static_cast<size_t>(id) + 1, -1.0f);
            float& v = memo[id];
            if (v >= 0.0f)
                return v;
            const std::string& n = BlockRegistry::Instance().Name(id);
            const auto has = [&](const char* t) { return n.contains(t); };
            if (has("bedrock") || has("barrier") || has("command_block") || has("end_portal") || has("end_gateway") ||
                has("structure_block") || has("jigsaw") || has("reinforced_deepslate") || n == "minecraft:light_block" ||
                has("border_block") || has("allow") || has("deny"))
                v = 3600000.0f;
            else if (has("obsidian") || has("ancient_debris") || has("netherite_block") || has("respawn_anchor") ||
                     has("enchanting_table") || has("ender_chest"))
                v = 1200.0f;
            else if (has("anvil"))
                v = 1200.0f;
            else if (has("water") || has("lava"))
                v = 100.0f;
            else if (has("deepslate") || has("iron_block") || has("iron_door") || has("iron_bars") || has("copper") ||
                     has("diamond_block") || has("emerald_block") || has("gold_block") || has("bell") ||
                     has("hopper") || has("cauldron") || has("lodestone") || has("crafter"))
                v = 6.0f;
            else if (has("stone") || has("cobble") || has("brick") || has("andesite") || has("diorite") ||
                     has("granite") || has("tuff") || has("basalt") || has("blackstone") || has("prismarine") ||
                     has("purpur") || has("calcite") || has("_ore") || (has("concrete") && !has("powder")) ||
                     has("terracotta") || has("quartz") || has("dripstone"))
                v = 6.0f;
            else if (has("planks") || has("_log") || has("_wood") || has("stem") || has("hyphae") || has("fence") ||
                     has("door") || has("chest") || has("barrel") || has("crafting_table") || has("bookshelf") ||
                     has("slab") || has("stairs"))
                v = 3.0f;
            else if (has("glass"))
                v = 0.3f;
            else if (has("leaves") || has("wool") || has("carpet") || has("snow") || has("cactus") || has("sponge"))
                v = 0.8f;
            else if (has("dirt") || has("grass_block") || has("sand") || has("gravel") || has("clay") || has("farmland") ||
                     has("mud") || has("podzol") || has("mycelium") || has("soul_soil") || has("netherrack"))
                v = 0.5f;
            else if (has("tnt"))
                v = 0.0f;
            else
                v = 1.0f;
            return v;
        }

    }
    using namespace detail;
}
