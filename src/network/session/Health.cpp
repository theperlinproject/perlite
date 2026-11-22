#include "network/session/SessionInternal.hpp"

namespace perlite {
    float Session::MaxHealth() const {
        return 20.0f + 4.0f * static_cast<float>(EffectLevel(21));
    }

    int Session::EffectLevel(int id) const {
        const auto it = effects.find(id);
        return it == effects.end() ? 0 : it->second.amplifier + 1;
    }

    int Session::ArmorValue() const {
        int points = 0;
        for (const ItemStack& a : inventory.armor)
            if (!a.Empty())
                points += ArmorPoints(a.networkId);
        return points;
    }

    void Session::SendEffect(int id, int event) {
        if (state != State::InGame)
            return;
        proto::MobEffectPacket p;
        p.runtimeEntityId = runtimeId;
        p.eventId = static_cast<proto::MobEffectPacketEventId>(event);
        p.effectId = id;
        if (const auto it = effects.find(id); it != effects.end()) {
            p.amplifier = it->second.amplifier;
            p.particles = it->second.particles;
            p.duration = it->second.ticks;
            p.ambient = it->second.ambient;
        }
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::AddEffect(int id, int amplifier, int ticks, bool particles, bool ambient) {
        if (id <= 0 || ticks <= 0)
            return;
        if (id == 6) {
            Heal(static_cast<float>(4 << std::clamp(amplifier, 0, 10)));
            return;
        }
        if (id == 7) {
            Damage(static_cast<float>(6 << std::clamp(amplifier, 0, 10)), DamageCause::Magic, posX, posZ);
            return;
        }
        if (id == 23) {
            SetFood(food + amplifier + 1, saturation + static_cast<float>(amplifier + 1) * 2.0f);
            return;
        }
        auto it = effects.find(id);
        const bool fresh = it == effects.end();
        if (!fresh && it->second.amplifier > amplifier)
            return;
        if (!fresh && it->second.amplifier == amplifier && it->second.ticks > ticks)
            return;
        ActiveEffect& e = effects[id];
        e.amplifier = amplifier;
        e.ticks = ticks;
        e.particles = particles;
        e.ambient = ambient;
        if (id == 22)
            absorption = std::max(absorption, 4.0f * static_cast<float>(amplifier + 1));
        SendEffect(id, fresh ? 1 : 2);
        attributesDirty = true;
    }

    bool Session::RemoveEffect(int id) {
        const auto it = effects.find(id);
        if (it == effects.end())
            return false;
        SendEffect(id, 3);
        effects.erase(it);
        if (id == 22)
            absorption = 0;
        if (id == 21)
            health = std::min(health, MaxHealth());
        attributesDirty = true;
        return true;
    }

    void Session::ClearEffects() {
        std::vector<int> ids;
        for (const auto& [id, e] : effects)
            ids.push_back(id);
        for (int id : ids)
            RemoveEffect(id);
    }

    void Session::SetFood(int value, float sat) {
        food = std::clamp(value, 0, 20);
        saturation = std::clamp(sat, 0.0f, static_cast<float>(food));
        attributesDirty = true;
    }

    void Session::Exhaust(float amount) {
        if (gameMode == 1 || gameMode == 6 || dead)
            return;
        exhaustion = std::min(40.0f, exhaustion + amount);
    }

    void Session::DamageArmor(float amount) {
        if (gameMode == 1 || gameMode == 6)
            return;
        const int loss = std::max(1, static_cast<int>(amount / 4.0f));
        bool changed = false;
        for (int slot = 0; slot < 4; slot++) {
            ItemStack& a = inventory.armor[static_cast<size_t>(slot)];
            if (a.Empty())
                continue;
            const int max = MaxDurability(a.networkId);
            if (max <= 0)
                continue;
            a.damage = static_cast<uint16_t>(std::min(max + 1, a.damage + loss));
            if (a.damage >= max) {
                a = ItemStack{};
                for (Session* s : server.PlayersIn(dimension))
                    s->PlaySound("Break", posX, posY, posZ);
            } else {
                a.stackId = inventory.NewStackId();
            }
            SendArmorSlot(slot);
            changed = true;
        }
        if (changed)
            server.BroadcastArmor(*this);
    }

    bool Session::Shielding(double fromX, double fromZ) const {
        const auto isShield = [](const ItemStack& s) {
            return !s.Empty() && ItemNameOf(s.networkId) == "minecraft:shield";
        };
        const bool raised = shieldUp && isShield(shieldOff ? inventory.offhand : inventory.MainHeld());
        if (!sneaking && !raised)
            return false;
        if (!isShield(inventory.offhand) && !isShield(inventory.MainHeld()))
            return false;
        const double yawRad = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const double lookX = -std::sin(yawRad), lookZ = std::cos(yawRad);
        const double dx = fromX - posX, dz = fromZ - posZ;
        const double len = std::sqrt(dx * dx + dz * dz);
        return len < 1e-6 || (dx * lookX + dz * lookZ) / len > 0.0;
    }

    void Session::Hurt(float amount, double fromX, double fromZ) {
        Damage(amount, DamageCause::Mob, fromX, fromZ);
    }

    void Session::Kill() {
        Damage(std::numeric_limits<float>::max() / 4.0f, DamageCause::Kill, posX, posZ);
    }

    bool Session::Damage(float amount, DamageCause cause, double fromX, double fromZ, const std::string& source,
                         Session* attacker, float knockback) {
        if (dead || amount <= 0 || state != State::InGame)
            return false;
        const bool bypassInvulnerable = cause == DamageCause::Void || cause == DamageCause::Kill;
        if ((gameMode == 1 || gameMode == 6) && !bypassInvulnerable)
            return false;
        if (FireCause(cause) && (EffectLevel(12) > 0 || !server.RuleValue("firedamage")))
            return false;
        if (cause == DamageCause::Fall && !server.RuleValue("falldamage"))
            return false;
        if (cause == DamageCause::Drowning && !server.RuleValue("drowningdamage"))
            return false;
        if (cause == DamageCause::Starving && server.Config().difficulty == 0)
            return false;
        if (server.HookPlayerDamage(*this, amount, cause, attacker, source) || amount <= 0)
            return false;
        const bool blockable = cause == DamageCause::Mob || cause == DamageCause::Player ||
                               cause == DamageCause::Projectile || cause == DamageCause::Explosion;
        if (blockable && Shielding(fromX, fromZ)) {
            for (Session* s : server.PlayersIn(dimension))
                s->PlaySound("ShieldBlock", posX, posY, posZ);
            ItemStack& shield = ItemNameOf(inventory.offhand.networkId) == "minecraft:shield"
                                    ? inventory.offhand
                                    : inventory.main[static_cast<size_t>(inventory.selected)];
            if (amount >= 3.0f && gameMode != 1) {
                const int max = MaxDurability(shield.networkId);
                shield.damage = static_cast<uint16_t>(shield.damage + 1 + static_cast<int>(amount));
                if (max > 0 && shield.damage >= max)
                    shield = ItemStack{};
                else
                    shield.stackId = inventory.NewStackId();
                SendInventory();
            }
            return false;
        }
        float dealt = amount;
        if (invulnerable > 10 && !bypassInvulnerable) {
            if (amount <= lastHurt)
                return false;
            dealt = amount - lastHurt;
            lastHurt = amount;
        } else {
            lastHurt = amount;
            invulnerable = 20;
        }
        if (!BypassesArmor(cause)) {
            float armor = 0, toughness = 0;
            for (const ItemStack& a : inventory.armor)
                if (!a.Empty()) {
                    armor += static_cast<float>(ArmorPoints(a.networkId));
                    toughness += ArmorToughness(a.networkId);
                }
            if (armor > 0) {
                DamageArmor(dealt);
                const float effective =
                    std::clamp(armor - 4.0f * dealt / (toughness + 8.0f), armor / 5.0f, 20.0f);
                dealt *= 1.0f - effective / 25.0f;
            }
        }
        if (const int resistance = EffectLevel(11); resistance > 0 && cause != DamageCause::Void && cause != DamageCause::Kill)
            dealt *= std::max(0.0f, 1.0f - 0.2f * static_cast<float>(resistance));
        if (dealt <= 0)
            return false;
        if (absorption > 0) {
            const float used = std::min(absorption, dealt);
            absorption -= used;
            dealt -= used;
        }
        health -= dealt;
        Exhaust(0.1f);
        attributesDirty = true;
        for (Session* s : server.PlayersIn(dimension))
            s->MobEvent(runtimeId, 2, 0);
        const bool pushes = cause == DamageCause::Mob || cause == DamageCause::Player ||
                            cause == DamageCause::Projectile || cause == DamageCause::Explosion;
        if (pushes && knockback > 0) {
            float resist = 0;
            for (const ItemStack& a : inventory.armor)
                if (!a.Empty())
                    resist += KnockbackResistance(a.networkId);
            const float strength = knockback * std::max(0.0f, 1.0f - resist);
            const double dx = posX - fromX, dz = posZ - fromZ;
            const double len = std::max(0.01, std::sqrt(dx * dx + dz * dz));
            SetVelocity(dx / len * strength, std::min(0.4f, 0.36f * strength / 0.4f + 0.04f),
                        dz / len * strength);
        }
        if (health <= 0)
            Die(cause, attacker ? attacker->Name() : source);
        else
            SendAttributes();
        return true;
    }

    void Session::Die(DamageCause cause, const std::string& source) {
        server.HookPlayerDeath(*this, cause, source);
        dead = true;
        health = 0;
        StopUsing();
        SendAttributes();
        for (Session* s : server.InGamePlayers())
            if (s != this)
                s->MobEvent(runtimeId, 3, 0);
        if (!server.RuleValue("keepinventory")) {
            const auto drop = [&](ItemStack& slot) {
                if (slot.Empty())
                    return;
                std::uniform_real_distribution<double> unit(-0.2, 0.2);
                static std::mt19937 rng{std::random_device{}()};
                server.SpawnItem(slot, posX, posY - 1.0, posZ, unit(rng), 0.2, unit(rng), 40, dimension);
                slot = ItemStack{};
            };
            for (ItemStack& slot : inventory.main)
                drop(slot);
            for (ItemStack& slot : inventory.armor)
                drop(slot);
            drop(inventory.offhand);
            drop(inventory.cursor);
            for (ItemStack& slot : inventory.craft)
                drop(slot);
            const int xp = std::min(xpLevel * 7, 100);
            server.SpawnXp(dimension, posX, posY - 1.0, posZ, xp);
            xpLevel = 0;
            xpProgress = 0;
            xpTotal = 0;
            SendInventory();
            server.BroadcastArmor(*this);
        }
        ClearEffects();
        fireTicks = 0;
        air = 300;
        fallDistance = 0;
        std::string message;
        const std::string from = source.empty() ? std::string() : Pretty(source);
        switch (cause) {
        case DamageCause::Mob:
            message = name + " was slain by " + (from.empty() ? std::string("a mob") : from);
            break;
        case DamageCause::Player:
            message = name + " was slain by " + from;
            break;
        case DamageCause::Projectile:
            message = name + " was shot by " + (from.empty() ? std::string("an arrow") : from);
            break;
        case DamageCause::Explosion:
            message = from.empty() ? name + " blew up" : name + " was blown up by " + from;
            break;
        case DamageCause::Fall:
            message = name + " fell from a high place";
            break;
        case DamageCause::Void:
            message = name + " fell out of the world";
            break;
        case DamageCause::Lava:
            message = name + " tried to swim in lava";
            break;
        case DamageCause::Fire:
            message = name + " went up in flames";
            break;
        case DamageCause::Burning:
            message = name + " burned to death";
            break;
        case DamageCause::Drowning:
            message = name + " drowned";
            break;
        case DamageCause::Starving:
            message = name + " starved to death";
            break;
        case DamageCause::Suffocation:
            message = name + " suffocated in a wall";
            break;
        case DamageCause::Cactus:
            message = name + " was pricked to death";
            break;
        case DamageCause::Magma:
            message = name + " discovered the floor was lava";
            break;
        case DamageCause::BerryBush:
            message = name + " was poked to death by a sweet berry bush";
            break;
        case DamageCause::Wither:
            message = name + " withered away";
            break;
        case DamageCause::Magic:
        case DamageCause::Poison:
            message = name + " was killed by magic";
            break;
        case DamageCause::FallingBlock:
            message = name + " was squashed by a falling block";
            break;
        default:
            message = name + " died";
            break;
        }
        if (server.RuleValue("showdeathmessages"))
            server.Broadcast(message);
    }

    void Session::Heal(float amount) {
        if (dead || amount <= 0 || health >= MaxHealth())
            return;
        health = std::min(MaxHealth(), health + amount);
        attributesDirty = true;
    }

    void Session::AddExperience(int points) {
        if (points <= 0 || dead)
            return;
        xpTotal += points;
        float progress = xpProgress + static_cast<float>(points) / static_cast<float>(XpForLevel(xpLevel));
        const int before = xpLevel;
        while (progress >= 1.0f) {
            progress = (progress - 1.0f) * static_cast<float>(XpForLevel(xpLevel));
            xpLevel++;
            progress /= static_cast<float>(XpForLevel(xpLevel));
        }
        xpProgress = progress;
        if (xpLevel > before && xpLevel % 5 == 0)
            PlaySound("LevelUp", posX, posY, posZ);
        attributesDirty = true;
    }

    void Session::AddLevels(int levels) {
        xpLevel = std::clamp(xpLevel + levels, 0, 24791);
        if (xpLevel == 0 && levels < 0)
            xpProgress = 0;
        if (levels > 0 && xpLevel % 5 == 0)
            PlaySound("LevelUp", posX, posY, posZ);
        attributesDirty = true;
    }

    float Session::AttackDamage(const ItemStack& held) const {
        float damage = 1.0f;
        if (!held.Empty()) {
            const std::string n = ItemNameOf(held.networkId);
            const auto tier = [&](float wood, float stone, float iron, float gold, float diamond, float netherite,
                                  float copper) {
                if (n.contains("wooden_"))
                    return wood;
                if (n.contains("stone_"))
                    return stone;
                if (n.contains("iron_"))
                    return iron;
                if (n.contains("golden_"))
                    return gold;
                if (n.contains("diamond_"))
                    return diamond;
                if (n.contains("netherite_"))
                    return netherite;
                if (n.contains("copper_"))
                    return copper;
                return 1.0f;
            };
            if (n.ends_with("_sword"))
                damage = tier(5, 6, 7, 5, 8, 9, 6);
            else if (n.ends_with("_axe"))
                damage = tier(4, 5, 6, 4, 7, 8, 5);
            else if (n.ends_with("_pickaxe"))
                damage = tier(3, 4, 5, 3, 6, 7, 4);
            else if (n.ends_with("_shovel"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n.ends_with("_hoe"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n.ends_with("_spear"))
                damage = tier(2, 3, 4, 2, 5, 6, 3);
            else if (n == "minecraft:trident")
                damage = 9.0f;
            else if (n == "minecraft:mace") {
                damage = 6.0f;
                if (!onGround && fallDistance > 1.5f) {
                    const float f = fallDistance;
                    damage += 4.0f * std::min(f, 3.0f) + 2.0f * std::clamp(f - 3.0f, 0.0f, 5.0f) +
                              std::max(0.0f, f - 8.0f);
                }
            }
        }
        if (const int sharp = held.Enchant(9); sharp > 0)
            damage += 1.25f * static_cast<float>(sharp);
        damage += 3.0f * static_cast<float>(EffectLevel(5));
        damage -= 4.0f * static_cast<float>(EffectLevel(18));
        const bool falling = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
        if (falling && EffectLevel(15) == 0)
            damage *= 1.5f;
        return std::max(0.0f, damage);
    }

    void Session::AttackPlayer(Session& victim, const ItemStack& held) {
        if (&victim == this || !victim.InGame() || victim.Dead() || victim.dimension != dimension ||
            !server.RuleValue("pvp"))
            return;
        if (victim.gameMode == 1 || victim.gameMode == 6)
            return;
        const double dx = victim.posX - posX, dy = victim.posY - posY, dz = victim.posZ - posZ;
        if (dx * dx + dy * dy + dz * dz > 7.0 * 7.0)
            return;
        const bool crit = !onGround && fallDistance > 0.0f && !swimming && !gliding && !flying;
        const float damage = AttackDamage(held);
        const float knock = sprinting ? 0.9f : 0.4f;
        if (victim.Damage(damage, DamageCause::Player, posX, posZ, name, this, knock)) {
            if (crit)
                for (Session* s : server.PlayersIn(dimension))
                    s->CritAnimation(victim.runtimeId);
            const std::string n = held.Empty() ? std::string() : ItemNameOf(held.networkId);
            if (n.ends_with("_sword") || n == "minecraft:trident" || n == "minecraft:mace")
                DamageHeld(1);
            else if (n.ends_with("_axe") || n.ends_with("_pickaxe") || n.ends_with("_shovel") || n.ends_with("_hoe"))
                DamageHeld(2);
            if (sprinting)
                sprinting = false;
        }
        Exhaust(0.1f);
    }

    void Session::FoodTick() {
        if (gameMode == 1 || gameMode == 6 || dead)
            return;
        const int difficulty = server.Config().difficulty;
        if (const int hunger = EffectLevel(17); hunger > 0)
            Exhaust(0.005f * static_cast<float>(hunger));
        if (exhaustion > 4.0f) {
            exhaustion -= 4.0f;
            if (saturation > 0)
                saturation = std::max(0.0f, saturation - 1.0f);
            else if (difficulty != 0)
                food = std::max(0, food - 1);
            attributesDirty = true;
        }
        const bool regen = server.RuleValue("naturalregeneration") != 0;
        if (difficulty == 0) {
            if (regen && health < MaxHealth() && server.TickCount() % 20 == 0)
                Heal(1.0f);
            if (food < 20 && server.TickCount() % 10 == 0) {
                food++;
                attributesDirty = true;
            }
        }
        if (regen && saturation > 0 && health < MaxHealth() && food >= 20) {
            if (++foodTimer >= 10) {
                const float amount = std::min(saturation, 6.0f);
                Heal(amount / 6.0f);
                Exhaust(amount);
                foodTimer = 0;
            }
        } else if (regen && food >= 18 && health < MaxHealth()) {
            if (++foodTimer >= 80) {
                Heal(1.0f);
                Exhaust(6.0f);
                foodTimer = 0;
            }
        } else if (food <= 0) {
            if (++foodTimer >= 80) {
                if (health > 10.0f || difficulty == 3 || (health > 1.0f && difficulty == 2))
                    Damage(1.0f, DamageCause::Starving, posX, posZ);
                foodTimer = 0;
            }
        } else {
            foodTimer = 0;
        }
    }

    void Session::EffectTick() {
        std::vector<int> expired;
        for (auto& [id, e] : effects) {
            const int amp = e.amplifier;
            if (id == 10) {
                const int interval = std::max(1, 50 >> amp);
                if (e.ticks % interval == 0)
                    Heal(1.0f);
            } else if (id == 19 || id == 25) {
                const int interval = std::max(1, 25 >> amp);
                if (e.ticks % interval == 0 && (health > 1.0f || id == 25))
                    Damage(1.0f, DamageCause::Poison, posX, posZ);
            } else if (id == 20) {
                const int interval = std::max(1, 40 >> amp);
                if (e.ticks % interval == 0)
                    Damage(1.0f, DamageCause::Wither, posX, posZ);
            }
            if (--e.ticks <= 0)
                expired.push_back(id);
        }
        for (int id : expired)
            RemoveEffect(id);
    }

    void Session::EnvironmentTick() {
        if (gameMode == 1 || gameMode == 6 || dead) {
            if (fireTicks > 0 || air < 300) {
                fireTicks = 0;
                air = 300;
                dataDirty = true;
            }
            return;
        }
        envTicks++;
        const double feetY = posY - 1.62;
        const std::string feet = BlockNameAt(posX, feetY + 0.1, posZ);
        const std::string body = BlockNameAt(posX, feetY + 1.0, posZ);
        const std::string eye = BlockNameAt(posX, posY, posZ);
        const std::string below = BlockNameAt(posX, feetY - 0.2, posZ);
        const auto any = [&](const char* what) {
            return feet.contains(what) || body.contains(what);
        };
        const bool inWater = any("water") || any("bubble_column");
        const bool inLava = any("lava");
        if (eye.contains("water") && EffectLevel(13) == 0) {
            air--;
            if (air <= -20) {
                air = 0;
                Damage(2.0f, DamageCause::Drowning, posX, posZ);
            }
            dataDirty = true;
        } else if (air < 300) {
            air = std::min(300, air + 4);
            dataDirty = true;
        }
        if (inLava) {
            if (EffectLevel(12) == 0)
                fireTicks = std::max(fireTicks, 300);
            Damage(4.0f, DamageCause::Lava, posX, posZ);
            fallDistance = 0;
        } else if (any("fire") && !any("campfire")) {
            fireTicks = std::max(fireTicks, 160);
            Damage(1.0f, DamageCause::Fire, posX, posZ);
        } else if (any("campfire") && !any("soul_campfire")) {
            Damage(1.0f, DamageCause::Fire, posX, posZ);
        } else if (any("soul_campfire")) {
            Damage(2.0f, DamageCause::Fire, posX, posZ);
        }
        if (inWater || (server.Raining() && dimension == kOverworld && fireTicks > 0 && envTicks % 20 == 0 &&
                        BlockNameAt(posX, posY + 1.0, posZ).ends_with(":air"))) {
            if (fireTicks > 0)
                dataDirty = true;
            fireTicks = 0;
        }
        if (fireTicks > 0) {
            if (EffectLevel(12) > 0) {
                fireTicks = 0;
                dataDirty = true;
            } else {
                if (fireTicks % 20 == 0 && !inLava)
                    Damage(1.0f, DamageCause::Burning, posX, posZ);
                if (--fireTicks == 0)
                    dataDirty = true;
            }
        }
        if (any("cactus"))
            Damage(1.0f, DamageCause::Cactus, posX, posZ);
        if (any("sweet_berry_bush") && (std::abs(posX - lastMoveX) > 0.003 || std::abs(posZ - lastMoveZ) > 0.003))
            Damage(1.0f, DamageCause::BerryBush, posX, posZ);
        if (below.contains("magma") && !sneaking && EffectLevel(12) == 0 && onGround)
            Damage(1.0f, DamageCause::Magma, posX, posZ);
        if (!eye.empty() && envTicks % 10 == 0) {
            const uint16_t id = BlockRegistry::Instance().Find(eye);
            if (id != 0 && BlockSolid(id) && !BlockLiquid(id) && !eye.contains("glass") && !eye.contains("leaves") &&
                !eye.contains("slab") && !eye.contains("stairs") && !eye.contains("fence") && !eye.contains("door") &&
                !eye.contains("wall") && !eye.contains("pane") && !eye.contains("chest") && !eye.contains("trapdoor"))
                Damage(1.0f, DamageCause::Suffocation, posX, posZ);
        }
        if ((fireTicks > 0) != lastFireSent)
            dataDirty = true;
    }

    void Session::SurvivalTick() {
        if (invulnerable > 0)
            invulnerable--;
        if (!dead && !awaitingDimensionAck && posY - 1.62 < RangeOf(dimension).minY - 64)
            Damage(4.0f, DamageCause::Void, posX, posZ);
        if (!dead) {
            EffectTick();
            FoodTick();
            EnvironmentTick();
            if (useTicks >= 0) {
                const ItemStack& usedNow = useOff ? inventory.offhand : inventory.MainHeld();
                if (inventory.selected != useSlot || usedNow.networkId != useItem) {
                    StopUsing();
                } else {
                    useTicks++;
                    const std::string n = ItemNameOf(useItem);
                    if (n != "minecraft:bow") {
                        const FoodInfo* f = FoodOf(n);
                        if (useTicks % 4 == 0 && f && f->nutrition > 0 && useTicks < f->ticks)
                            for (Session* s : server.PlayersIn(dimension))
                                if (s != this)
                                    s->MobEvent(runtimeId, 57, static_cast<int>(useItem) << 16);
                        if (f && useTicks >= f->ticks)
                            FinishUsing();
                    }
                }
            }
        }
        if (dataDirty || air != lastAirSent || (fireTicks > 0) != lastFireSent) {
            dataDirty = false;
            lastAirSent = air;
            lastFireSent = fireTicks > 0;
            SendSelfData();
        }
        if (attributesDirty) {
            attributesDirty = false;
            SendAttributes();
        }
    }

    std::vector<Session::EffectState> Session::Effects() const {
        std::vector<EffectState> out;
        for (const auto& [id, e] : effects)
            out.push_back({id, e.amplifier, e.ticks, e.particles, e.ambient});
        return out;
    }

    void Session::SetHealth(float value) {
        if (dead || state != State::InGame)
            return;
        health = std::min(value, MaxHealth());
        if (health <= 0) {
            Die(DamageCause::Kill, "plugin");
            return;
        }
        SendAttributes();
    }
}
