#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::SendInventory() {
        const auto content = [&](proto::WindowIDVarint window, proto::ContainerSlotType type,
                                 const ItemStack* stacks, size_t n) {
            proto::InventoryContentPacket p;
            p.windowId = window;
            p.container.containerId = type;
            for (size_t i = 0; i < n; i++)
                p.input.push_back(ToWire(stacks[i]));
            Send(p);
        };
        content(proto::WindowIDVarint::Inventory, proto::ContainerSlotType::HotbarAndInventory,
                inventory.main.data(), inventory.main.size());
        content(proto::WindowIDVarint::Armor, proto::ContainerSlotType::Armor, inventory.armor.data(),
                inventory.armor.size());
        content(proto::WindowIDVarint::Offhand, proto::ContainerSlotType::Offhand, &inventory.offhand, 1);
        SendHeldSelection();
    }

    void Session::SendSlot(int slot) {
        if (slot < 0 || slot >= PlayerInventory::kMainSlots)
            return;
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Inventory;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::HotbarAndInventory;
        p.container = c;
        p.item = ToWire(inventory.main[static_cast<size_t>(slot)]);
        Send(p);
    }

    void Session::SendHeldSelection() {
        proto::MobEquipmentPacket m;
        m.runtimeEntityId = runtimeId;
        m.item = ToWire(inventory.MainHeld());
        m.slot = static_cast<uint8_t>(inventory.selected);
        m.selectedSlot = static_cast<uint8_t>(inventory.selected);
        m.windowId = proto::WindowID::Inventory;
        Send(m);
    }

    void Session::OnStackRequest(std::span<const uint8_t> body) {
        proto::ItemStackRequestPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        proto::ItemStackResponsePacket reply;
        bool armorChanged = false;
        bool containerChanged = false;
        for (const proto::ItemStackRequest& r : p.requests) {
            const bool furnace = inventory.containerKind >= 2 && inventory.container.size() == 3;
            const ItemStack smelted = furnace ? inventory.container[2] : ItemStack{};
            inventory.levels = xpLevel;
            const ItemStack enchantBefore = inventory.craft[14];
            const RequestResult result = ApplyStackRequest(inventory, r, creative);
            if (result.ok && result.levelsSpent > 0)
                AddLevels(-result.levelsSpent);
            if (result.ok && result.enchanted)
                inventory.enchantSeed = static_cast<uint32_t>(std::random_device{}());
            if (result.ok && inventory.containerKind == 12 && (result.enchanted || !inventory.craft[14].Same(enchantBefore) ||
                                                                inventory.craft[14].count != enchantBefore.count))
                SendEnchantOffers();
            if (result.ok && furnace && !smelted.Empty() && inventory.container.size() == 3) {
                const ItemStack& now = inventory.container[2];
                const int taken = now.Empty() || now.networkId != smelted.networkId ? smelted.count : smelted.count - now.count;
                if (taken > 0) {
                    const std::string out = ItemNameOf(smelted.networkId);
                    float each = 0.1f;
                    if (out.ends_with("_ingot") || out == "minecraft:netherite_scrap")
                        each = out.contains("gold") ? 1.0f : out.contains("netherite") ? 2.0f : 0.7f;
                    else if (out == "minecraft:diamond" || out == "minecraft:emerald")
                        each = 1.0f;
                    else if (out == "minecraft:redstone" || out == "minecraft:lapis_lazuli" || out == "minecraft:quartz")
                        each = out == "minecraft:redstone" ? 0.3f : 0.2f;
                    else if (out.starts_with("minecraft:cooked_") || out == "minecraft:baked_potato" ||
                             out == "minecraft:dried_kelp" || out.ends_with("terracotta"))
                        each = 0.35f;
                    else if (out == "minecraft:charcoal" || out == "minecraft:sponge")
                        each = 0.15f;
                    else if (out == "minecraft:green_dye" || out == "minecraft:lime_dye")
                        each = 1.0f;
                    static std::mt19937 rng{std::random_device{}()};
                    const float total = each * static_cast<float>(taken);
                    int whole = static_cast<int>(std::floor(total));
                    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < total - static_cast<float>(whole))
                        whole++;
                    AddExperience(whole);
                }
            }
            if (result.ok)
                for (const auto& [container, slot] : result.touched)
                    if (container != proto::ContainerSlotType::HotbarAndInventory && container != proto::ContainerSlotType::Hotbar &&
                        container != proto::ContainerSlotType::Inventory && container != proto::ContainerSlotType::Armor &&
                        container != proto::ContainerSlotType::Offhand && container != proto::ContainerSlotType::Cursor &&
                        container != proto::ContainerSlotType::CraftingInput)
                        containerChanged = true;
            reply.responses.push_back(MakeResponse(inventory, r.requestId, result));
            if (result.ok)
                for (const auto& [container, slot] : result.touched)
                    if (container == proto::ContainerSlotType::Armor) {
                        armorChanged = true;
                        break;
                    }
            if (result.ok)
                for (const ItemStack& thrown : result.dropped)
                    server.DropFromPlayer(*this, thrown);
        }
        if (armorChanged)
            server.BroadcastArmor(*this);
        if (containerChanged)
            server.SyncContainer(*this);
        if (!reply.responses.empty())
            Send(reply);
    }

    void Session::OnMobEquipment(std::span<const uint8_t> body) {
        proto::MobEquipmentPacket m;
        if (!Decode(body, m) || state != State::InGame)
            return;
        if (m.selectedSlot < PlayerInventory::kHotbar) {
            if (inventory.selected != m.selectedSlot)
                StopUsing();
            inventory.selected = m.selectedSlot;
        }
    }

    void Session::OnContainerClose(std::span<const uint8_t> body) {
        proto::ContainerClosePacket c;
        if (!Decode(body, c) || state != State::InGame)
            return;
        server.CloseContainer(*this);
        proto::ContainerClosePacket ack;
        ack.windowId = c.windowId;
        ack.windowType = openType;
        ack.server = false;
        Send(ack);
        openType = proto::WindowType::Inventory;
    }

    void Session::OnPickBlock(std::span<const uint8_t> body) {
        proto::BlockPickRequestPacket p;
        if (!Decode(body, p) || state != State::InGame || !InReach(p.x, p.y, p.z))
            return;
        std::string blockName;
        if (server.GetBlock(p.x, p.y, p.z, blockName) != perlin::abi::Status::Ok)
            return;
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(blockName);
        if (!item)
            return;
        int slot = -1;
        for (int i = 0; i < PlayerInventory::kHotbar; i++)
            if (!inventory.main[static_cast<size_t>(i)].Empty() &&
                inventory.main[static_cast<size_t>(i)].networkId == item->networkId)
                slot = i;
        if (slot < 0 && creative) {
            slot = inventory.selected;
            for (int i = 0;
                 i < PlayerInventory::kHotbar && !inventory.main[static_cast<size_t>(slot)].Empty(); i++)
                if (inventory.main[static_cast<size_t>(i)].Empty())
                    slot = i;
            inventory.main[static_cast<size_t>(slot)] =
                inventory.MakeStack(item->networkId, MaxStack(item->networkId));
            SendSlot(slot);
        }
        if (slot < 0)
            return;
        inventory.selected = slot;
        SendHeldSelection();
    }

    Session::HandScope::HandScope(Session& s, bool off)
        : self(s), was(s.inventory.offActive), on(off && !s.inventory.offhand.Empty()) {
        self.inventory.offActive = on;
    }

    Session::HandScope::~HandScope() {
        self.inventory.offActive = was;
        if (on && !was)
            self.SendInventory();
    }

    void Session::SwapOffhand() {
        if (isVisitor || gameMode == 6)
            return;
        std::swap(inventory.main[static_cast<size_t>(inventory.selected)], inventory.offhand);
        SendInventory();
        server.BroadcastArmor(*this);
    }

    bool Session::EquipHeldArmor() {
        ItemStack& held = inventory.main[static_cast<size_t>(inventory.selected)];
        if (held.Empty() || held.count != 1)
            return false;
        const int slot = ArmorSlotFor(held.networkId);
        if (slot < 0)
            return false;
        std::swap(held, inventory.armor[static_cast<size_t>(slot)]);
        if (!held.Empty())
            held.stackId = inventory.NewStackId();
        inventory.armor[static_cast<size_t>(slot)].stackId = inventory.NewStackId();
        SendSlot(inventory.selected);
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Armor;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::Armor;
        p.container = c;
        p.item = ToWire(inventory.armor[static_cast<size_t>(slot)]);
        Send(p);
        SendHeldSelection();
        server.BroadcastArmor(*this);
        return true;
    }

    void Session::OpenWindow(int window, proto::WindowType type, int x, int y, int z) {
        openWindow = window;
        openType = type;
        proto::ContainerOpenPacket open;
        open.windowId = static_cast<proto::WindowID>(window);
        open.windowType = type;
        open.coordinates = {x, y, z};
        open.runtimeEntityId = -1;
        Send(open);
    }

    void Session::SendWindowContents(proto::ContainerSlotType type, const std::vector<ItemStack>& slots) {
        if (openWindow < 0)
            return;
        proto::InventoryContentPacket p;
        p.windowId = static_cast<proto::WindowIDVarint>(openWindow);
        p.container.containerId = type;
        for (const ItemStack& s : slots)
            p.input.push_back(ToWire(s));
        Send(p);
    }

    void Session::SendWindowData(int property, int value) {
        if (openWindow < 0)
            return;
        proto::ContainerSetDataPacket p;
        p.windowId = static_cast<proto::WindowID>(openWindow);
        p.property = property;
        p.value = value;
        Send(p);
    }

    void Session::CloseWindowFromServer() {
        if (openWindow < 0)
            return;
        proto::ContainerClosePacket p;
        p.windowId = static_cast<proto::WindowID>(openWindow);
        p.windowType = openType;
        p.server = true;
        Send(p);
        openWindow = -1;
    }

    void Session::DamageHeld(int amount) {
        if (gameMode == 1 || gameMode == 6)
            return;
        ItemStack& held = inventory.HeldRef();
        if (held.Empty())
            return;
        const int max = MaxDurability(held.networkId);
        if (max <= 0)
            return;
        held.damage = static_cast<uint16_t>(std::min(max + 1, held.damage + amount));
        if (held.damage >= max) {
            held = ItemStack{};
            for (Session* s : server.PlayersIn(dimension))
                s->PlaySound("Break", posX, posY, posZ);
        } else {
            held.stackId = inventory.NewStackId();
        }
        SendSlot(inventory.selected);
        SendHeldSelection();
    }

    void Session::SendArmorSlot(int slot) {
        proto::InventorySlotPacket p;
        p.windowId = proto::WindowIDVarint::Armor;
        p.slot = static_cast<uint32_t>(slot);
        proto::FullContainerName c;
        c.containerId = proto::ContainerSlotType::Armor;
        p.container = c;
        p.item = ToWire(inventory.armor[static_cast<size_t>(slot)]);
        Send(p);
    }

    // The enchanting table's three offers for the item in it (the game's costs and enchantments).
    void Session::SendEnchantOffers() {
        if (inventory.enchantSeed == 0)
            inventory.enchantSeed = static_cast<uint32_t>(std::random_device{}()) | 1u;
        inventory.enchantOffers = EnchantOffers(inventory.craft[14], inventory.bookshelves, inventory.enchantSeed);
        proto::PlayerEnchantOptionsPacket p;
        static const char* const kWords[] = {"the", "elder", "scrolls", "klaatu", "berata", "niktu", "xyzzy", "bless", "curse", "light",
                                             "darkness", "fire", "air", "earth", "water", "hot", "dry", "cold", "wet", "ignite"};
        for (size_t i = 0; i < inventory.enchantOffers.size(); i++) {
            const PlayerInventory::EnchantOffer& o = inventory.enchantOffers[i];
            proto::EnchantOption e;
            e.cost = static_cast<uint8_t>(std::min(o.cost, 255));
            e.slotFlags = 0;
            for (const auto& [id, level] : o.enchants)
                e.selfEnchants.push_back({static_cast<uint8_t>(id), static_cast<uint8_t>(level)});
            e.name = std::string(kWords[(inventory.enchantSeed + i) % 20]) + " " + kWords[(inventory.enchantSeed / 7 + i * 3) % 20];
            e.optionId = o.optionId;
            p.options.push_back(std::move(e));
        }
        Send(p);
    }
}
