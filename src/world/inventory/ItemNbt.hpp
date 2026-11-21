#pragma once
#include "protocol/NbtTree.hpp"
#include "world/inventory/Inventory.hpp"

#include <span>

namespace perlite {
    nbt::Value ItemToNbt(const ItemStack& stack, int slot);
    bool ItemFromNbt(const nbt::Value& tag, PlayerInventory* stackIds, ItemStack& out);
    nbt::Value ItemListToNbt(std::span<const ItemStack> items);
    void ItemListFromNbt(const nbt::Value* list, PlayerInventory* stackIds, std::span<ItemStack> slots);
}
