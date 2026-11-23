// The rules of the crafting stations that are more than a recipe: the enchanting table's offers
// (the game's cost and selection), the anvil's cost and merging, brewing, and armour trims.
#include "world/inventory/Inventory.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/vanilla/JavaRandom.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace perlite {
    namespace {
        std::string NameOf(int16_t id) {
            const CatalogItem* item = BlockCatalog::Instance().ItemById(id);
            if (!item)
                return {};
            return item->name.starts_with("minecraft:") ? item->name.substr(10) : item->name;
        }
        int16_t IdOf(const char* name) {
            const CatalogItem* item = BlockCatalog::Instance().ItemByName(std::string("minecraft:") + name);
            return item ? item->networkId : 0;
        }
        bool Ends(const std::string& s, const char* t) { return s.ends_with(t); }

        // What an enchantment goes on.
        enum Slot { Armor = 1, Helmet = 2, Chest = 4, Legs = 8, Boots = 16, Sword = 32, Tool = 64, Bow = 128, Rod = 256, Trident = 512,
                    Crossbow = 1024, Mace = 2048, Durable = 4096 };

        struct EnchantInfo {
            int id, maxLevel, weight;
            int minBase, minPer, span; // min cost = minBase + (level-1)*minPer; max = min + span
            int slots;
            bool treasure;
            int group; // enchantments of the same group exclude each other (0: none)
        };
        // The game's enchantment table (ids as Bedrock numbers them).
        const EnchantInfo kEnchants[] = {
            {0, 4, 10, 1, 11, 11, Armor, false, 1},        {1, 4, 5, 10, 8, 8, Armor, false, 1},
            {2, 4, 5, 5, 6, 6, Boots, false, 0},           {3, 4, 2, 5, 8, 8, Armor, false, 1},
            {4, 4, 5, 3, 6, 6, Armor, false, 1},           {5, 3, 1, 10, 20, 50, Chest, false, 0},
            {6, 3, 2, 10, 10, 30, Helmet, false, 0},       {7, 3, 2, 10, 10, 15, Boots, false, 2},
            {8, 1, 2, 1, 0, 40, Helmet, false, 0},         {9, 5, 10, 1, 11, 20, Sword, false, 3},
            {10, 5, 5, 5, 8, 20, Sword, false, 3},         {11, 5, 5, 5, 8, 20, Sword, false, 3},
            {12, 2, 5, 5, 20, 50, Sword, false, 0},        {13, 2, 2, 10, 20, 50, Sword, false, 0},
            {14, 3, 2, 15, 9, 50, Sword, false, 0},        {15, 5, 10, 1, 10, 50, Tool, false, 0},
            {16, 1, 1, 15, 0, 50, Tool, false, 4},         {17, 3, 5, 5, 8, 50, Durable, false, 0},
            {18, 3, 2, 15, 9, 50, Tool, false, 4},         {19, 5, 10, 1, 10, 15, Bow, false, 0},
            {20, 2, 2, 12, 20, 25, Bow, false, 0},         {21, 1, 2, 20, 0, 30, Bow, false, 0},
            {22, 1, 1, 20, 0, 30, Bow, false, 5},          {23, 3, 2, 15, 9, 50, Rod, false, 0},
            {24, 3, 2, 15, 9, 50, Rod, false, 0},          {25, 2, 2, 10, 10, 15, Boots, true, 2},
            {26, 1, 2, 25, 0, 50, Durable, true, 5},       {27, 1, 1, 25, 0, 25, Armor, true, 0},
            {28, 1, 1, 25, 0, 25, Durable, true, 0},       {29, 5, 2, 1, 8, 20, Trident, false, 0},
            {30, 3, 2, 17, 7, 50, Trident, false, 6},      {31, 3, 5, 12, 7, 50, Trident, false, 7},
            {32, 1, 1, 25, 0, 25, Trident, false, 7},      {33, 1, 2, 20, 0, 30, Crossbow, false, 8},
            {34, 4, 10, 1, 10, 50, Crossbow, false, 8},    {35, 3, 5, 12, 20, 50, Crossbow, false, 0},
            {36, 3, 1, 10, 10, 15, Boots, true, 0},        {37, 3, 1, 25, 25, 50, Legs, true, 0},
            {38, 3, 2, 15, 10, 50, Mace, true, 0},         {39, 5, 5, 5, 8, 20, Mace, false, 9},
            {40, 4, 2, 15, 9, 50, Mace, false, 9},
        };

        const EnchantInfo* Info(int id) {
            for (const EnchantInfo& e : kEnchants)
                if (e.id == id)
                    return &e;
            return nullptr;
        }

        int SlotsOf(const std::string& n) {
            int s = 0;
            if (Ends(n, "_helmet") || n == "turtle_helmet")
                s |= Armor | Helmet | Durable;
            if (Ends(n, "_chestplate"))
                s |= Armor | Chest | Durable;
            if (Ends(n, "_leggings"))
                s |= Armor | Legs | Durable;
            if (Ends(n, "_boots"))
                s |= Armor | Boots | Durable;
            if (Ends(n, "_sword"))
                s |= Sword | Durable;
            if (Ends(n, "_axe"))
                s |= Tool | Durable; // axes take the sword's damage enchantments from books only
            if (Ends(n, "_pickaxe") || Ends(n, "_shovel") || Ends(n, "_hoe"))
                s |= Tool | Durable;
            if (n == "bow")
                s |= Bow | Durable;
            if (n == "crossbow")
                s |= Crossbow | Durable;
            if (n == "fishing_rod")
                s |= Rod | Durable;
            if (n == "trident")
                s |= Trident | Durable;
            if (n == "mace")
                s |= Mace | Durable;
            if (n == "shears" || n == "flint_and_steel" || n == "shield" || n == "elytra" || n == "carrot_on_a_stick" || n == "brush")
                s |= Durable;
            return s;
        }

        int Enchantability(const std::string& n) {
            if (n == "book")
                return 1;
            const bool armor = SlotsOf(n) & Armor;
            if (n.starts_with("wooden_"))
                return 15;
            if (n.starts_with("stone_"))
                return 5;
            if (n.starts_with("iron_"))
                return armor ? 9 : 14;
            if (n.starts_with("golden_"))
                return armor ? 25 : 22;
            if (n.starts_with("diamond_"))
                return 10;
            if (n.starts_with("netherite_"))
                return 15;
            if (n.starts_with("leather_"))
                return 15;
            if (n.starts_with("chainmail_"))
                return 12;
            if (n == "turtle_helmet")
                return 9;
            if (n == "bow" || n == "crossbow" || n == "fishing_rod" || n == "trident")
                return 1;
            if (n == "mace")
                return 15;
            return 0;
        }

        // Java's Random: the game draws the enchanting table's numbers from it.
        using Random = vanilla::WorldRandom;
    }

    bool Enchantable(int16_t networkId) {
        const std::string n = NameOf(networkId);
        return Enchantability(n) > 0 && (n == "book" || (SlotsOf(n) & ~Durable) != 0);
    }

    std::vector<PlayerInventory::EnchantOffer> EnchantOffers(const ItemStack& item, int bookshelves, uint32_t seed) {
        std::vector<PlayerInventory::EnchantOffer> out;
        if (item.Empty() || !item.enchants.empty() || !Enchantable(item.networkId))
            return out;
        const std::string n = NameOf(item.networkId);
        const bool book = n == "book";
        const int itemSlots = book ? ~0 : SlotsOf(n);
        const int ench = Enchantability(n);
        const int b = std::min(bookshelves, 15);
        Random costs = Random::Legacy(seed);
        int levels[3];
        for (int slot = 0; slot < 3; slot++) {
            const int i = costs.NextInt(8) + 1 + (b >> 1) + costs.NextInt(b + 1);
            levels[slot] = slot == 0 ? std::max(i / 3, 1) : slot == 1 ? i * 2 / 3 + 1 : std::max(i, b * 2);
            if (levels[slot] < slot + 1)
                levels[slot] = 0;
        }
        for (int slot = 0; slot < 3; slot++) {
            if (levels[slot] <= 0)
                continue;
            Random r = Random::Legacy(seed + static_cast<uint32_t>(slot));
            int level = levels[slot] + 1 + r.NextInt(ench / 4 + 1) + r.NextInt(ench / 4 + 1);
            const float f = (r.NextFloat() + r.NextFloat() - 1.0f) * 0.15f;
            level = std::max(1, static_cast<int>(std::lround(level + level * f)));
            // Every enchantment that fits at this power, at its highest level that does.
            const auto available = [&](const std::vector<std::pair<int16_t, int16_t>>& chosen) {
                std::vector<std::pair<const EnchantInfo*, int>> list;
                for (const EnchantInfo& e : kEnchants) {
                    if (e.treasure || !(e.slots & itemSlots))
                        continue;
                    if (!book && (e.slots == Sword) && Ends(n, "_axe"))
                        continue;
                    bool clash = false;
                    for (const auto& [cid, cl] : chosen) {
                        const EnchantInfo* c = Info(cid);
                        clash = clash || cid == e.id || (c && c->group != 0 && c->group == e.group);
                    }
                    if (clash)
                        continue;
                    for (int l = e.maxLevel; l >= 1; l--) {
                        const int min = e.minBase + (l - 1) * e.minPer, max = min + e.span;
                        if (level >= min && level <= max) {
                            list.push_back({&e, l});
                            break;
                        }
                    }
                }
                return list;
            };
            PlayerInventory::EnchantOffer offer;
            offer.cost = levels[slot];
            offer.optionId = static_cast<int32_t>(seed % 100000) * 4 + slot + 1;
            const auto pick = [&](std::vector<std::pair<const EnchantInfo*, int>>& list) {
                int total = 0;
                for (const auto& [e, l] : list)
                    total += e->weight;
                if (total <= 0)
                    return false;
                int w = r.NextInt(total);
                for (const auto& [e, l] : list) {
                    w -= e->weight;
                    if (w < 0) {
                        offer.enchants.push_back({static_cast<int16_t>(e->id), static_cast<int16_t>(l)});
                        return true;
                    }
                }
                return false;
            };
            auto list = available(offer.enchants);
            if (!pick(list))
                continue;
            while (r.NextInt(50) <= level) {
                list = available(offer.enchants);
                if (list.empty() || !pick(list))
                    break;
                level /= 2;
            }
            if (book && offer.enchants.size() > 1)
                offer.enchants.erase(offer.enchants.begin() + r.NextInt(static_cast<int>(offer.enchants.size())));
            out.push_back(std::move(offer));
        }
        return out;
    }

    ItemStack AnvilResult(const ItemStack& left, const ItemStack& right, const std::string& rename, int& cost, int& materialUsed) {
        cost = 0;
        materialUsed = 0;
        if (left.Empty())
            return {};
        ItemStack out = left;
        out.count = 1;
        int work = left.repairCost + (right.Empty() ? 0 : right.repairCost);
        int extra = 0;
        const std::string ln = NameOf(left.networkId), rn = right.Empty() ? "" : NameOf(right.networkId);
        const int maxDamage = MaxDurability(left.networkId);
        if (!right.Empty()) {
            // Repair with the material it is made of: a quarter of its durability per unit.
            const auto material = [&]() -> std::string {
                if (ln.starts_with("diamond_"))
                    return "diamond";
                if (ln.starts_with("iron_") || ln.starts_with("chainmail_"))
                    return "iron_ingot";
                if (ln.starts_with("golden_"))
                    return "gold_ingot";
                if (ln.starts_with("netherite_"))
                    return "netherite_ingot";
                if (ln.starts_with("leather_"))
                    return "leather";
                if (ln.starts_with("stone_"))
                    return "cobblestone";
                if (ln.starts_with("wooden_"))
                    return "oak_planks";
                if (ln == "turtle_helmet")
                    return "turtle_scute";
                if (ln == "elytra")
                    return "phantom_membrane";
                if (ln == "mace")
                    return "breeze_rod";
                return "";
            }();
            if (!material.empty() && (rn == material || (material == "oak_planks" && Ends(rn, "_planks"))) && maxDamage > 0 && left.damage > 0) {
                int damage = left.damage;
                while (damage > 0 && materialUsed < right.count) {
                    damage = std::max(0, damage - maxDamage / 4);
                    materialUsed++;
                    extra++;
                }
                out.damage = static_cast<uint16_t>(damage);
            } else if (rn == ln || rn == "enchanted_book") {
                const bool fromBook = rn == "enchanted_book";
                if (!fromBook && maxDamage > 0) {
                    // Two of the same: their durability added, plus 12%.
                    const int leftHas = maxDamage - left.damage, rightHas = maxDamage - right.damage;
                    const int has = std::min(maxDamage, leftHas + rightHas + maxDamage * 12 / 100);
                    if (has > leftHas) {
                        out.damage = static_cast<uint16_t>(maxDamage - has);
                        extra += 2;
                    }
                }
                const int slots = ln == "book" || ln == "enchanted_book" ? ~0 : SlotsOf(ln);
                for (const auto& [id, level] : right.enchants) {
                    const EnchantInfo* e = Info(id);
                    if (!e || !(e->slots & slots))
                        continue;
                    bool clash = false;
                    for (const auto& [oid, ol] : out.enchants) {
                        const EnchantInfo* o = Info(oid);
                        clash = clash || (oid != id && o && o->group != 0 && o->group == e->group);
                    }
                    if (clash) {
                        extra++;
                        continue;
                    }
                    int newLevel = level;
                    bool found = false;
                    for (auto& [oid, ol] : out.enchants)
                        if (oid == id) {
                            newLevel = ol == level ? std::min<int>(level + 1, e->maxLevel) : std::max<int>(ol, level);
                            ol = static_cast<int16_t>(newLevel);
                            found = true;
                        }
                    if (!found)
                        out.enchants.push_back({id, level});
                    const int rarity = e->weight >= 10 ? 1 : e->weight >= 5 ? 2 : e->weight >= 2 ? 4 : 8;
                    extra += std::max(1, fromBook ? rarity / 2 : rarity) * newLevel;
                }
                materialUsed = 1;
            } else {
                return {};
            }
        }
        if (!rename.empty() && rename != left.name) {
            out.name = rename.substr(0, 50);
            extra += 1;
        } else if (rename.empty() && !left.name.empty() && right.Empty()) {
            out.name.clear();
            extra += 1;
        }
        if (extra == 0)
            return {};
        cost = work + extra;
        out.repairCost = std::max(left.repairCost, right.Empty() ? 0 : right.repairCost) * 2 + 1;
        return out;
    }

    namespace {
        // The game's brewing recipes: potion ids as Bedrock numbers them.
        struct Brew {
            int from;
            const char* ingredient;
            int to;
        };
        const Brew kBrews[] = {
            {0, "nether_wart", 4}, {0, "redstone", 1}, {0, "glowstone_dust", 3}, {0, "fermented_spider_eye", 34},
            {4, "golden_carrot", 5}, {5, "redstone", 6}, {5, "fermented_spider_eye", 7}, {6, "fermented_spider_eye", 8},
            {7, "redstone", 8}, {4, "rabbit_foot", 9}, {9, "redstone", 10}, {9, "glowstone_dust", 11},
            {4, "magma_cream", 12}, {12, "redstone", 13}, {4, "sugar", 14}, {14, "redstone", 15}, {14, "glowstone_dust", 16},
            {14, "fermented_spider_eye", 17}, {15, "fermented_spider_eye", 18}, {9, "fermented_spider_eye", 17},
            {10, "fermented_spider_eye", 18}, {17, "redstone", 18}, {17, "glowstone_dust", 42}, {4, "pufferfish", 19},
            {19, "redstone", 20}, {4, "glistering_melon_slice", 21}, {21, "glowstone_dust", 22}, {21, "fermented_spider_eye", 23},
            {22, "fermented_spider_eye", 24}, {25, "fermented_spider_eye", 23}, {26, "fermented_spider_eye", 23},
            {27, "fermented_spider_eye", 24}, {23, "glowstone_dust", 24}, {4, "spider_eye", 25}, {25, "redstone", 26},
            {25, "glowstone_dust", 27}, {4, "ghast_tear", 28}, {28, "redstone", 29}, {28, "glowstone_dust", 30},
            {4, "blaze_powder", 31}, {31, "redstone", 32}, {31, "glowstone_dust", 33}, {31, "fermented_spider_eye", 34},
            {32, "fermented_spider_eye", 35}, {34, "redstone", 35}, {4, "turtle_helmet", 37}, {37, "redstone", 38},
            {37, "glowstone_dust", 39}, {4, "phantom_membrane", 40}, {40, "redstone", 41}, {4, "breeze_rod", 43},
            {4, "cobweb", 44}, {4, "slime_block", 45}, {4, "stone", 46},
        };
    }

    void ForEachBrew(const std::function<void(int, const std::string&, int)>& fn) {
        for (const Brew& b : kBrews)
            fn(b.from, b.ingredient, b.to);
    }

    bool BrewIngredient(int16_t ingredient) {
        const std::string n = NameOf(ingredient);
        if (n == "gunpowder" || n == "dragon_breath")
            return true;
        for (const Brew& b : kBrews)
            if (n == b.ingredient)
                return true;
        return false;
    }

    bool BrewResult(int16_t potionItem, uint16_t potionMeta, int16_t ingredient, int16_t& outItem, uint16_t& outMeta) {
        const std::string p = NameOf(potionItem), n = NameOf(ingredient);
        if (p != "potion" && p != "splash_potion" && p != "lingering_potion")
            return false;
        // Gunpowder makes a splash potion, dragon's breath a lingering one.
        if (n == "gunpowder" && p == "potion") {
            outItem = IdOf("splash_potion");
            outMeta = potionMeta;
            return outItem != 0;
        }
        if (n == "dragon_breath" && p == "splash_potion") {
            outItem = IdOf("lingering_potion");
            outMeta = potionMeta;
            return outItem != 0;
        }
        for (const Brew& b : kBrews)
            if (b.from == potionMeta && n == b.ingredient) {
                outItem = potionItem;
                outMeta = static_cast<uint16_t>(b.to);
                return true;
            }
        return false;
    }

    bool TrimOf(const std::string& templateItem, const std::string& material, std::string& pattern, std::string& materialId) {
        std::string t = templateItem.starts_with("minecraft:") ? templateItem.substr(10) : templateItem;
        if (!t.ends_with("_armor_trim_smithing_template"))
            return false;
        pattern = t.substr(0, t.size() - std::string_view("_armor_trim_smithing_template").size());
        static const std::map<std::string, std::string> kMaterials = {
            {"iron_ingot", "iron"},       {"copper_ingot", "copper"},   {"gold_ingot", "gold"},     {"lapis_lazuli", "lapis"},
            {"emerald", "emerald"},       {"diamond", "diamond"},       {"netherite_ingot", "netherite"},
            {"redstone", "redstone"},     {"amethyst_shard", "amethyst"}, {"quartz", "quartz"},     {"resin_brick", "resin"}};
        const std::string m = material.starts_with("minecraft:") ? material.substr(10) : material;
        const auto it = kMaterials.find(m);
        if (it == kMaterials.end())
            return false;
        materialId = it->second;
        return true;
    }
}
