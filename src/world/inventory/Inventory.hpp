#pragma once
#include <functional>
#include "protocol/Packets.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlite {

    struct ItemStack {
        int16_t networkId = 0;
        uint16_t count = 0;
        int32_t stackId = 0;
        uint16_t damage = 0;
        std::vector<std::pair<int16_t, int16_t>> enchants;
        uint16_t meta = 0;      // the item's aux value: the potion of a potion, the colour of a banner
        std::string name;       // renamed in an anvil
        int32_t repairCost = 0; // what earlier anvil work adds to the next
        std::vector<std::pair<std::string, int32_t>> patterns; // banner patterns: code and colour
        std::string trimPattern, trimMaterial;                 // armour trim
        bool Empty() const { return networkId == 0 || count == 0; }
        bool Same(const ItemStack& o) const {
            return networkId == o.networkId && damage == o.damage && enchants == o.enchants && meta == o.meta && name == o.name &&
                   repairCost == o.repairCost && patterns == o.patterns && trimPattern == o.trimPattern && trimMaterial == o.trimMaterial;
        }
        int Enchant(int id) const {
            for (const auto& [e, level] : enchants)
                if (e == id)
                    return level;
            return 0;
        }
    };

    int EnchantId(const std::string& name);

    int MaxStack(int16_t networkId);
    int ArmorSlotFor(int16_t networkId);
    int MaxDurability(int16_t networkId);
    int ArmorPoints(int16_t networkId);
    float ArmorToughness(int16_t networkId);
    float KnockbackResistance(int16_t networkId);

    class PlayerInventory {
    public:
        static constexpr int kMainSlots = 36;
        static constexpr int kHotbar = 9;

        std::array<ItemStack, kMainSlots> main;
        std::array<ItemStack, 4> armor;
        ItemStack offhand;
        ItemStack cursor;
        ItemStack output;
        std::array<ItemStack, 54> craft; // the ui slots: crafting grid 28..40, station inputs (anvil 1, loom 9, grindstone 16, smithing 51..53, ...)
        std::vector<ItemStack> container;
        int containerKind = -1;
        // Stations: the player's levels (for anvil and enchanting costs, set before each request),
        // the enchanting table's three offers and its bookshelves, the enchanting seed.
        int levels = 0;
        struct EnchantOffer {
            int32_t optionId = 0;
            int cost = 0;
            std::vector<std::pair<int16_t, int16_t>> enchants;
        };
        std::vector<EnchantOffer> enchantOffers;
        int bookshelves = 0;
        uint32_t enchantSeed = 0;
        int selected = 0;
        bool offActive = false;

        ItemStack* Resolve(proto2225::ContainerSlotType type, int slot);

        const ItemStack& Held() const { return offActive ? offhand : main[static_cast<size_t>(selected)]; }
        ItemStack& HeldRef() { return offActive ? offhand : main[static_cast<size_t>(selected)]; }
        const ItemStack& MainHeld() const { return main[static_cast<size_t>(selected)]; }
        ItemStack MakeStack(int16_t networkId, int count);
        int Give(int16_t networkId, int count);
        int Give(const ItemStack& stack);
        bool TakeHeld(int count);
        int32_t NewStackId() { return nextStackId++; }

    private:
        int32_t nextStackId = 1;
    };

    struct RequestResult {
        bool ok = false;
        std::vector<std::pair<proto2225::ContainerSlotType, int>> touched;
        std::vector<ItemStack> dropped;
        int levelsSpent = 0;
        bool enchanted = false;
    };

    // The stations' rules (Stations.cpp).
    std::vector<PlayerInventory::EnchantOffer> EnchantOffers(const ItemStack& item, int bookshelves, uint32_t seed);
    bool Enchantable(int16_t networkId);
    // An anvil's result for the two inputs and a new name; cost in levels (0: nothing to do).
    ItemStack AnvilResult(const ItemStack& left, const ItemStack& right, const std::string& rename, int& cost, int& materialUsed);
    // A brewing step: the potion (item and meta) after the ingredient, false when it does not brew.
    bool BrewResult(int16_t potionItem, uint16_t potionMeta, int16_t ingredient, int16_t& outItem, uint16_t& outMeta);
    bool BrewIngredient(int16_t ingredient);
    void ForEachBrew(const std::function<void(int from, const std::string& ingredient, int to)>& fn);
    // The armour trim a smithing template and material give.
    bool TrimOf(const std::string& templateItem, const std::string& material, std::string& pattern, std::string& materialId);

    RequestResult ApplyStackRequest(PlayerInventory& inv, const proto2225::ItemStackRequest& request,
                                    bool creative);

    proto2225::ItemStackResponsesEntry MakeResponse(PlayerInventory& inv, int32_t requestId,
                                                    const RequestResult& result);

    proto2225::ItemV4 ToWire(const ItemStack& stack);
}
