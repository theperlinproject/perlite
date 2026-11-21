#include "world/inventory/ItemNbt.hpp"

#include "world/level/block/BlockCatalog.hpp"

#include <algorithm>

namespace perlite {
    nbt::Value ItemToNbt(const ItemStack& stack, int slot) {
        nbt::Value item = nbt::Value::Make(nbt::Compound);
        const CatalogItem* catalog = BlockCatalog::Instance().ItemById(stack.networkId);
        item.SetString("Name", catalog ? catalog->name : std::string());
        item.SetByte("Count", static_cast<int8_t>(std::min<int>(stack.count, 127)));
        item.SetShort("Damage", static_cast<int16_t>(stack.damage));
        item.SetByte("Slot", static_cast<int8_t>(slot));
        item.SetByte("WasPickedUp", 0);
        if (!stack.enchants.empty()) {
            nbt::Value tag = nbt::Value::Make(nbt::Compound);
            nbt::Value ench = nbt::Value::Make(nbt::List);
            ench.listType = nbt::Compound;
            for (const auto& [id, level] : stack.enchants) {
                nbt::Value e = nbt::Value::Make(nbt::Compound);
                e.SetShort("id", id);
                e.SetShort("lvl", level);
                ench.list.push_back(std::move(e));
            }
            tag.Set("ench", std::move(ench));
            item.Set("tag", std::move(tag));
        }
        return item;
    }

    bool ItemFromNbt(const nbt::Value& tag, PlayerInventory* stackIds, ItemStack& out) {
        std::string name = tag.String("Name");
        if (!name.empty() && !name.contains(':'))
            name = "minecraft:" + name;
        const CatalogItem* catalog = BlockCatalog::Instance().ItemByName(name);
        const int count = static_cast<int>(tag.Int("Count", 0));
        if (!catalog || count <= 0)
            return false;
        out = ItemStack{};
        out.networkId = catalog->networkId;
        out.count = static_cast<uint16_t>(std::min(count, MaxStack(catalog->networkId)));
        out.stackId = stackIds ? stackIds->NewStackId() : 0;
        out.damage = static_cast<uint16_t>(tag.Int("Damage", 0));
        if (const nbt::Value* extra = tag.Find("tag"))
            if (const nbt::Value* ench = extra->Find("ench"))
                for (const nbt::Value& e : ench->list)
                    out.enchants.emplace_back(static_cast<int16_t>(e.Int("id")),
                                              static_cast<int16_t>(e.Int("lvl")));
        return true;
    }

    nbt::Value ItemListToNbt(std::span<const ItemStack> items) {
        nbt::Value list = nbt::Value::Make(nbt::List);
        list.listType = nbt::Compound;
        for (size_t i = 0; i < items.size(); i++)
            if (!items[i].Empty())
                list.list.push_back(ItemToNbt(items[i], static_cast<int>(i)));
        return list;
    }

    void ItemListFromNbt(const nbt::Value* list, PlayerInventory* stackIds, std::span<ItemStack> slots) {
        if (!list)
            return;
        for (const nbt::Value& tag : list->list) {
            const int slot = static_cast<int>(tag.Int("Slot", -1));
            ItemStack stack;
            if (slot >= 0 && static_cast<size_t>(slot) < slots.size() && ItemFromNbt(tag, stackIds, stack))
                slots[static_cast<size_t>(slot)] = std::move(stack);
        }
    }
}
