#include "network/session/SessionInternal.hpp"

namespace perlite {
    bool Session::InReach(int x, int y, int z) const {
        const double dx = x + 0.5 - posX, dy = y + 0.5 - posY, dz = z + 0.5 - posZ;
        return dx * dx + dy * dy + dz * dz <= 12.0 * 12.0;
    }

    void Session::OnInteract(std::span<const uint8_t> body) {
        proto::InteractPacket i;
        if (!Decode(body, i) || state != State::InGame)
            return;
        if (i.actionId == proto::InteractPacketActionId::OpenInventory) {
            proto::ContainerOpenPacket open;
            open.windowId = proto::WindowID::Inventory;
            open.windowType = proto::WindowType::Inventory;
            open.coordinates = {static_cast<int32_t>(std::floor(posX)),
                                static_cast<int32_t>(std::floor(posY)),
                                static_cast<int32_t>(std::floor(posZ))};
            open.runtimeEntityId = -1;
            Send(open);
        }
    }

    void Session::OnPlayerAction(std::span<const uint8_t> body) {
        proto::PlayerActionPacket a;
        if (!Decode(body, a) || state != State::InGame)
            return;
        const auto& pos = a.position;
        if (a.action == proto::Action::DimensionChangeAck) {
            awaitingDimensionAck = false;
            return;
        }
        switch (a.action) {
        case proto::Action::SwapOffhand:
            SwapOffhand();
            break;
        case proto::Action::StartSprint:
        case proto::Action::StopSprint:
            sprinting = a.action == proto::Action::StartSprint && (food > 6 || gameMode == 1);
            dataDirty = true;
            break;
        case proto::Action::Swimming:
        case proto::Action::StopSwimming:
            swimming = a.action == proto::Action::Swimming;
            dataDirty = true;
            break;
        case proto::Action::StartGlide:
        case proto::Action::StopGlide:
            gliding = a.action == proto::Action::StartGlide;
            dataDirty = true;
            break;
        case proto::Action::Jump:
            Exhaust(sprinting ? 0.2f : 0.05f);
            break;
        case proto::Action::StopSleeping:
            WakeUp();
            break;
        case proto::Action::StartBreak:
            if (isVisitor || gameMode == 6)
                break;
            if (creative)
                server.PlayerBreak(*this, pos.x, pos.y, pos.z);
            break;
        case proto::Action::StopBreak:
        case proto::Action::CreativePlayerDestroyBlock:
            if (isVisitor || gameMode == 6)
                break;
            lastActive = std::chrono::steady_clock::now();
            server.PlayerBreak(*this, pos.x, pos.y, pos.z);
            break;
        default:
            break;
        }
    }

    void Session::OnTransaction(std::span<const uint8_t> body) {
        proto::InventoryTransactionPacket t;
        if (!Decode(body, t) || state != State::InGame)
            return;
        if (t.transaction.transactionType == proto::TransactionTransactionType::Normal) {
            // Throwing the held item (Q outside the inventory screen) arrives as a normal transaction: an
            // inventory slot that loses items plus a world-interaction action carrying what was thrown.
            if (isVisitor || dead || gameMode == 6)
                return;
            const proto::TransactionActionsEntry* fromSlot = nullptr;
            const proto::TransactionActionsEntry* thrown = nullptr;
            for (const proto::TransactionActionsEntry& a : t.transaction.actions) {
                if (a.sourceType == proto::TransactionActionsEntrySourceType::WorldInteraction && a.flags.value_or(0) == 0)
                    thrown = &a;
                else if (a.sourceType == proto::TransactionActionsEntrySourceType::Container && a.windowId.value_or(-1) == 0)
                    fromSlot = &a;
            }
            if (!thrown || !fromSlot || fromSlot->slot >= PlayerInventory::kMainSlots || thrown->newItem.count == 0)
                return;
            ItemStack& slot = inventory.main[fromSlot->slot];
            const int count = std::min<int>(thrown->newItem.count, slot.count);
            if (slot.Empty() || slot.networkId != thrown->newItem.networkId || count <= 0) {
                SendInventory();
                return;
            }
            ItemStack drop = slot;
            drop.count = static_cast<uint16_t>(count);
            slot.count = static_cast<uint16_t>(slot.count - count);
            if (slot.count == 0)
                slot = ItemStack{};
            lastActive = std::chrono::steady_clock::now();
            server.DropFromPlayer(*this, drop);
            SendInventory();
            return;
        }
        if (t.transaction.transactionType == proto::TransactionTransactionType::ItemUseOnEntity) {
            const auto* on = std::get_if<proto::TransactionTransactionDataCase1>(&t.transaction.transactionData);
            if (!on || isVisitor || gameMode == 6 || dead)
                return;
            lastActive = std::chrono::steady_clock::now();
            if (on->hotbarSlot >= 0 && on->hotbarSlot < PlayerInventory::kHotbar)
                inventory.selected = on->hotbarSlot;
            HandScope hand(*this, on->hand == 1);
            if (on->actionType == proto::TransactionTransactionDataCase1ActionType::Attack) {
                const ItemStack held = inventory.Held();
                lastAttackItem = held;
                float damage = AttackDamage(held);
                if (server.HookPlayerAttack(*this, on->entityRuntimeId, damage))
                    return;
                if (Session* victim = server.FindSession(on->entityRuntimeId)) {
                    AttackPlayer(*victim, held);
                    return;
                }
                const bool crit = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
                if (server.MobsIn(dimension).Attack(*this, on->entityRuntimeId, damage, sprinting ? 0.9f : 0.4f)) {
                    Exhaust(0.1f);
                    if (crit)
                        for (Session* s : server.PlayersIn(dimension))
                            s->CritAnimation(on->entityRuntimeId);
                    const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
                    if (n.ends_with("_sword") || n == "minecraft:trident" || n == "minecraft:mace")
                        DamageHeld(1);
                    else if (n.ends_with("_axe") || n.ends_with("_pickaxe") || n.ends_with("_shovel") || n.ends_with("_hoe"))
                        DamageHeld(2);
                    if (sprinting) {
                        sprinting = false;
                        dataDirty = true;
                    }
                }
            } else {
                server.MobsIn(dimension).Interact(*this, on->entityRuntimeId);
            }
            return;
        }
        if (t.transaction.transactionType == proto::TransactionTransactionType::ItemRelease) {
            const auto* release = std::get_if<proto::TransactionTransactionDataCase2>(&t.transaction.transactionData);
            if (!release || dead)
                return;
            if (shieldUp) {
                shieldUp = false;
                dataDirty = true;
            }
            HandScope hand(*this, useTicks >= 0 ? useOff : release->hand == 1);
            if (useTicks >= 0 && ItemNameOf(useItem) == "minecraft:bow") {
                ReleaseBow();
            } else if (release->actionType == proto::TransactionTransactionDataCase2ActionType::Consume && useTicks >= 0) {
                const FoodInfo* f = FoodOf(ItemNameOf(useItem));
                if (f && useTicks >= f->ticks - 6)
                    FinishUsing();
                else
                    StopUsing();
            } else {
                StopUsing();
            }
            return;
        }
        if (t.transaction.transactionType != proto::TransactionTransactionType::ItemUse)
            return;
        const auto* use = std::get_if<proto::TransactionUseItem>(&t.transaction.transactionData);
        if (!use)
            return;
        if (use->hotbarSlot >= 0 && use->hotbarSlot < PlayerInventory::kHotbar)
            inventory.selected = use->hotbarSlot;
        const auto& at = use->blockPosition;
        if (isVisitor || gameMode == 6)
            return;
        lastActive = std::chrono::steady_clock::now();
        HandScope hand(*this, use->hand == proto::TransactionUseItemHand::OffHand &&
                                  use->actionType != proto::TransactionUseItemActionType::BreakBlock);
        switch (use->actionType) {
        case proto::TransactionUseItemActionType::BreakBlock:
            server.PlayerBreak(*this, at.x, at.y, at.z);
            break;
        case proto::TransactionUseItemActionType::ClickBlock:
            if (server.HookPlayerUse(*this, at.x, at.y, at.z, use->face)) {
                CorrectBlock(at.x, at.y, at.z);
                SendInventory();
                break;
            }
            if (ThrowHeld())
                break;
            if ((sneaking && !inventory.Held().Empty()) || !server.UseBlock(*this, at.x, at.y, at.z)) {
                if (!EquipHeldArmor()) {
                    const ItemStack& held = inventory.Held();
                    const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
                    if (!held.Empty() && (FoodOf(n) || n == "minecraft:bow")) {
                        if (useTicks < 0)
                            StartUsing();
                    } else {
                        server.PlayerPlace(*this, at.x, at.y, at.z, use->face, use->clickPos.x, use->clickPos.y, use->clickPos.z);
                    }
                }
            }
            break;
        case proto::TransactionUseItemActionType::ClickAir:
            if (ThrowHeld())
                break;
            if (!inventory.Held().Empty() && ItemNameOf(inventory.Held().networkId) == "minecraft:bucket") {
                server.UseBucket(*this, "minecraft:bucket", false, 0, 0, 0, -1);
                break;
            }
            if (!inventory.Held().Empty() && ItemNameOf(inventory.Held().networkId) == "minecraft:shield") {
                shieldUp = true;
                shieldOff = inventory.offActive;
                dataDirty = true;
                break;
            }
            if (!EquipHeldArmor() && useTicks < 0)
                StartUsing();
            break;
        default:
            break;
        }
    }

    void Session::OnBlockMined(float hardness) {
        Exhaust(0.005f);
        if (hardness <= 0.0f || gameMode == 1)
            return;
        const ItemStack& held = inventory.Held();
        if (held.Empty())
            return;
        const std::string n = ItemNameOf(held.networkId);
        if (n.ends_with("_sword") || n.ends_with("trident") || n.ends_with("mace"))
            DamageHeld(2);
        else if (n.ends_with("_pickaxe") || n.ends_with("_axe") || n.ends_with("_shovel") || n.ends_with("_hoe") ||
                 n.ends_with("shears"))
            DamageHeld(1);
    }

    std::string Session::BlockNameAt(double x, double y, double z) const {
        ChunkManager& world = server.World(dimension);
        const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)),
                  bz = static_cast<int>(std::floor(z));
        const DimensionRange range = RangeOf(dimension);
        if (by < range.minY || by > range.MaxY() || !world.Peek({bx >> 4, bz >> 4}))
            return {};
        return BlockRegistry::Instance().Name(world.GetBlock(bx, by, bz));
    }

    void Session::StartUsing() {
        const ItemStack& held = inventory.Held();
        if (held.Empty()) {
            StopUsing();
            return;
        }
        const std::string n = ItemNameOf(held.networkId);
        useOff = inventory.offActive;
        if (n == "minecraft:bow") {
            bool ammo = gameMode == 1;
            for (const ItemStack& s : inventory.main)
                if (!s.Empty() && ItemNameOf(s.networkId) == "minecraft:arrow")
                    ammo = true;
            if (!ItemNameOf(inventory.offhand.networkId).empty() &&
                ItemNameOf(inventory.offhand.networkId) == "minecraft:arrow")
                ammo = true;
            if (!ammo)
                return;
            useTicks = 0;
            useSlot = inventory.selected;
            useItem = held.networkId;
            return;
        }
        const FoodInfo* f = FoodOf(n);
        if (!f)
            return;
        if (!f->always && food >= 20 && gameMode != 1)
            return;
        useTicks = 0;
        useSlot = inventory.selected;
        useItem = held.networkId;
    }

    void Session::StopUsing() {
        useTicks = -1;
        useSlot = -1;
        useItem = 0;
    }

    void Session::ReleaseBow() {
        const int ticks = useTicks;
        StopUsing();
        if (ticks < 0)
            return;
        float charge = static_cast<float>(ticks) / 20.0f;
        charge = (charge * charge + charge * 2.0f) / 3.0f;
        if (charge < 0.1f)
            return;
        charge = std::min(charge, 1.0f);
        const ItemStack bow = inventory.Held();
        const int power = bow.Enchant(19), punch = bow.Enchant(20), flame = bow.Enchant(21), infinity = bow.Enchant(22);
        bool pickup = gameMode != 1;
        if (gameMode != 1) {
            // Infinity keeps plain arrows (they cannot be picked up again); any other arrow is used.
            bool found = false;
            ItemStack* from = nullptr;
            int fromSlot = -1;
            if (ItemNameOf(inventory.offhand.networkId).ends_with("arrow"))
                from = &inventory.offhand;
            for (int i = 0; i < PlayerInventory::kMainSlots && !from; i++) {
                ItemStack& s = inventory.main[static_cast<size_t>(i)];
                if (!s.Empty() && ItemNameOf(s.networkId).ends_with("arrow")) {
                    from = &s;
                    fromSlot = i;
                }
            }
            if (from) {
                found = true;
                const bool plain = ItemNameOf(from->networkId) == "minecraft:arrow";
                if (infinity > 0 && plain) {
                    pickup = false;
                } else {
                    from->count = static_cast<uint16_t>(from->count - 1);
                    if (from->count == 0)
                        *from = ItemStack{};
                    else
                        from->stackId = inventory.NewStackId();
                    if (fromSlot >= 0)
                        SendSlot(fromSlot);
                    else
                        SendInventory();
                }
            }
            if (!found)
                return;
            DamageHeld(1);
        }
        const double yawRad = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const double pitchRad = static_cast<double>(pitch) * 3.14159265358979323846 / 180.0;
        const double dirX = -std::sin(yawRad) * std::cos(pitchRad), dirY = -std::sin(pitchRad),
                     dirZ = std::cos(yawRad) * std::cos(pitchRad);
        // power 3.0 x the draw, aimed off by uncertainty_base 1 (x 0.0075), as the vanilla player_arrow.
        const double speed = 3.0 * charge;
        static std::mt19937 aimRng{std::random_device{}()};
        std::normal_distribution<double> gauss(0.0, 0.0075);
        double ax = dirX + gauss(aimRng), ay = dirY + gauss(aimRng), az = dirZ + gauss(aimRng);
        const double alen = std::max(1e-6, std::sqrt(ax * ax + ay * ay + az * az));
        ax /= alen;
        ay /= alen;
        az /= alen;
        if (Mob* arrow = server.MobsIn(dimension).SpawnProjectile(projectile::Arrow, posX + dirX * 0.5, posY - 0.1 + dirY * 0.5,
                                                       posZ + dirZ * 0.5, ax * speed, ay * speed, az * speed,
                                                       runtimeId, pickup)) {
            arrow->crit = charge >= 1.0f;
            if (power > 0)
                arrow->arrowDamage += 0.5f * static_cast<float>(power) + 0.5f;
            arrow->punch = punch;
            arrow->flame = flame > 0;
        }
        for (Session* s : server.PlayersIn(dimension))
            s->PlaySound("Bow", posX, posY, posZ);
        Exhaust(0.0f);
    }

    void Session::FinishUsing() {
        HandScope hand(*this, useOff);
        ItemStack& held = inventory.HeldRef();
        if (held.Empty() || held.networkId != useItem) {
            StopUsing();
            return;
        }
        const std::string n = ItemNameOf(held.networkId);
        const FoodInfo* f = FoodOf(n);
        StopUsing();
        if (!f)
            return;
        if (server.HookItemConsume(*this, n)) {
            SendInventory();
            SendAttributes();
            return;
        }
        static std::mt19937 rng{std::random_device{}()};
        const auto chance = [&](float p) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < p; };
        if (gameMode != 1) {
            food = std::min(20, food + f->nutrition);
            saturation = std::min(static_cast<float>(food), saturation + f->saturation);
        }
        const std::string bare = n.substr(n.find(':') + 1);
        if (bare == "golden_apple") {
            AddEffect(10, 1, 100);
            AddEffect(22, 0, 2400);
        } else if (bare == "enchanted_golden_apple" || bare == "appleEnchanted") {
            AddEffect(10, 1, 600);
            AddEffect(22, 3, 2400);
            AddEffect(11, 0, 6000);
            AddEffect(12, 0, 6000);
        } else if (bare == "chicken" && chance(0.3f)) {
            AddEffect(17, 0, 600);
        } else if (bare == "rotten_flesh" && chance(0.8f)) {
            AddEffect(17, 0, 600);
        } else if (bare == "poisonous_potato" && chance(0.6f)) {
            AddEffect(19, 0, 100);
        } else if (bare == "spider_eye") {
            AddEffect(19, 0, 100);
        } else if (bare == "pufferfish") {
            AddEffect(17, 2, 300);
            AddEffect(9, 0, 300);
            AddEffect(19, 3, 1200);
        } else if (bare == "honey_bottle") {
            RemoveEffect(19);
        } else if (bare == "milk_bucket") {
            ClearEffects();
        } else if (bare == "chorus_fruit") {
            for (int attempt = 0; attempt < 16; attempt++) {
                const double tx = posX + std::uniform_real_distribution<double>(-8, 8)(rng);
                const double tz = posZ + std::uniform_real_distribution<double>(-8, 8)(rng);
                const double feet = posY - 1.62;
                for (int dy = 8; dy >= -8; dy--) {
                    const double ty = std::floor(feet) + dy;
                    const std::string below = BlockNameAt(tx, ty - 1, tz);
                    if (below.empty() || !BlockSolid(BlockRegistry::Instance().Find(below)))
                        continue;
                    if (BlockSolid(server.World(dimension).GetBlock(static_cast<int>(std::floor(tx)), static_cast<int>(ty),
                                                                    static_cast<int>(std::floor(tz)))) ||
                        BlockSolid(server.World(dimension).GetBlock(static_cast<int>(std::floor(tx)), static_cast<int>(ty) + 1,
                                                                    static_cast<int>(std::floor(tz)))))
                        continue;
                    Teleport(std::floor(tx) + 0.5, ty + 1.62, std::floor(tz) + 0.5);
                    fallDistance = 0;
                    attempt = 16;
                    break;
                }
            }
        }
        if (gameMode != 1) {
            held.count = static_cast<uint16_t>(held.count - 1);
            if (held.count == 0)
                held = ItemStack{};
            else
                held.stackId = inventory.NewStackId();
            if (f->leftover) {
                const CatalogItem* rest = BlockCatalog::Instance().ItemByName(f->leftover);
                if (rest) {
                    if (held.Empty())
                        held = inventory.MakeStack(rest->networkId, 1);
                    else if (inventory.Give(rest->networkId, 1) > 0) {
                        ItemStack spill;
                        spill.networkId = rest->networkId;
                        spill.count = 1;
                        server.DropFromPlayer(*this, spill);
                    }
                }
            }
            SendInventory();
        }
        for (Session* s : server.PlayersIn(dimension)) {
            s->MobEvent(runtimeId, 57, static_cast<int>(held.networkId) << 16);
            s->PlaySound("Burp", posX, posY, posZ);
        }
        attributesDirty = true;
    }

    // Snowballs, eggs, ender pearls and bottles o' enchanting fly from the eyes the way the player looks.
    bool Session::ThrowHeld() {
        const ItemStack& held = inventory.Held();
        if (held.Empty())
            return false;
        const std::string n = ItemNameOf(held.networkId);
        int type = -1;
        double speed = 1.5;
        float aimPitch = pitch;
        if (n == "minecraft:snowball")
            type = projectile::Snowball;
        else if (n == "minecraft:egg" || n == "minecraft:blue_egg" || n == "minecraft:brown_egg")
            type = projectile::Egg;
        else if (n == "minecraft:ender_pearl")
            type = projectile::EnderPearl;
        else if (n == "minecraft:experience_bottle") {
            type = projectile::XpBottle;
            speed = 0.5;
            aimPitch -= 20.0f;
        }
        if (type < 0)
            return false;
        const auto now = server.TickCount();
        if (type == projectile::EnderPearl && now < pearlReadyAt)
            return true;
        const double yawRad = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const double pitchRad = static_cast<double>(aimPitch) * 3.14159265358979323846 / 180.0;
        const double dirX = -std::sin(yawRad) * std::cos(pitchRad), dirY = -std::sin(pitchRad),
                     dirZ = std::cos(yawRad) * std::cos(pitchRad);
        server.MobsIn(dimension).SpawnProjectile(type, posX + dirX * 0.3, posY - 0.1 + dirY * 0.3, posZ + dirZ * 0.3, dirX * speed,
                                      dirY * speed, dirZ * speed, runtimeId);
        if (type == projectile::EnderPearl)
            pearlReadyAt = now + 20;
        for (Session* s : server.PlayersIn(dimension))
            s->PlaySound("Throw", posX, posY, posZ);
        if (gameMode != 1) {
            inventory.TakeHeld(1);
            SendSlot(inventory.selected);
        }
        lastActive = std::chrono::steady_clock::now();
        return true;
    }
}
