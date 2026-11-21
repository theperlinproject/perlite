#include "world/actor/ActorInternal.hpp"

namespace perlite {
    bool ActorManager::Hurt(Mob& m, float damage, double fromX, double fromZ, Session* by, float knockback) {
        if (m.deathTime > 0 || m.kind != actorkind::Mob || damage <= 0)
            return false;
        if (!m.projectile && (server.HookEntityDamage(m, damage, by) || damage <= 0))
            return false;
        float dealt = damage;
        if (m.invulnerable > 10) {
            if (damage <= m.lastHurt)
                return false;
            dealt = damage - m.lastHurt;
            m.lastHurt = damage;
        } else {
            m.lastHurt = damage;
            m.invulnerable = 20;
            m.hurtTime = 10;
        }
        m.health -= dealt;
        if (by) {
            m.lastAttacker = by->Id();
            m.lastAttackerTicks = 100;
        }
        if (knockback > 0) {
            const double dx = m.x - fromX, dz = m.z - fromZ;
            const double len = std::max(0.01, std::sqrt(dx * dx + dz * dz));
            m.vx = m.vx * 0.5 + dx / len * knockback;
            m.vz = m.vz * 0.5 + dz / len * knockback;
            m.vy = m.onGround ? std::min(0.4, 0.36 * knockback / 0.4 + 0.04) : m.vy;
        }
        if (m.def->flags & mobflag::Panic)
            m.panicTicks = 60 + static_cast<int>(rng() % 40);
        if (by && (m.def->flags & (mobflag::Neutral | mobflag::Hostile))) {
            m.target = by->Id();
            m.angry = true;
        }
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->MobEvent(m.id, 2, 0);
            s->MobMotion(m);
        }
        if (m.health <= 0)
            Die(m, by);
        return true;
    }

    bool ActorManager::Attack(Session& by, uint64_t entity, float damage, float knockback) {
        Mob* m = Find(entity);
        if (!m || m->projectile || m->kind != actorkind::Mob)
            return false;
        const double dx = m->x - by.X(), dz = m->z - by.Z(), dy = m->y - (by.Y() - 1.62);
        if (dx * dx + dy * dy + dz * dz > 7.0 * 7.0)
            return false;
        return Hurt(*m, by.GameMode() == 1 ? std::max(damage, 1000.0f) : damage, by.X(), by.Z(), &by, knockback);
    }

    bool ActorManager::Interact(Session& by, uint64_t entity) {
        Mob* m = Find(entity);
        if (!m)
            return false;
        (void)by;
        return true;
    }

    void ActorManager::Die(Mob& m, Session* killer) {
        m.deathTime = 1;
        m.health = 0;
        m.target = 0;
        for (Session* s : server.PlayersIn(kOverworld))
            s->MobEvent(m.id, 3, 0);
        if (m.projectile || m.kind != actorkind::Mob)
            return;
        if (!killer && m.lastAttacker && m.lastAttackerTicks > 0)
            killer = server.FindSession(m.lastAttacker);
        if (killer && (!killer->InGame() || killer->DimensionId() != kOverworld))
            killer = nullptr;
        server.HookEntityDeath(m, killer);
        const bool hostile = (m.def->flags & mobflag::Hostile) != 0;
        const std::string java = LootTables::JavaEntityName(m.def->id);
        const bool zombieLike = java == "zombie" || java == "husk" || java == "drowned" || java == "zombie_villager" ||
                                java == "zombified_piglin" || java == "piglin" || java == "hoglin";
        if (m.baby && !zombieLike)
            return;
        if (killer)
            SpawnXpOrbs(m.x, m.y + 0.5, m.z, LootTables::MobXp(m.def->id, m.baby, hostile, rng));
        if (!server.RuleValue("domobloot"))
            return;
        std::vector<LootDrop> drops;
        const LootTables& loot = LootTables::Instance();
        if (loot.Has("entities/" + java)) {
            LootContext ctx;
            ctx.rng = &rng;
            ctx.killedByPlayer = killer != nullptr;
            ctx.onFire = m.burning;
            ctx.baby = m.baby;
            ctx.cubeSize = java == "magma_cube" ? 2 : 1;
            ctx.attackerType = killer ? "minecraft:player" : "";
            if (killer) {
                const ItemStack& held = killer->LastAttackItem();
                if (const CatalogItem* item = held.Empty() ? nullptr : BlockCatalog::Instance().ItemById(held.networkId))
                    ctx.tool = item->name;
                ctx.looting = held.Enchant(14);
            }
            drops = loot.Roll("entities/" + java, ctx);
        } else {
            std::istringstream list(m.def->drops);
            std::string entry;
            while (std::getline(list, entry, ';')) {
                std::istringstream parts(entry);
                std::string name, lo, hi, chance;
                std::getline(parts, name, ':');
                std::getline(parts, lo, ':');
                std::getline(parts, hi, ':');
                std::getline(parts, chance, ':');
                if (const auto it = ItemAliases().find(name); it != ItemAliases().end())
                    name = it->second;
                const double c = chance.empty() ? 1.0 : std::atof(chance.c_str());
                if (std::uniform_real_distribution<double>(0, 1)(rng) >= c)
                    continue;
                const int a = std::atoi(lo.c_str()), b = std::max(a, std::atoi(hi.c_str()));
                const int count = a + static_cast<int>(rng() % static_cast<unsigned>(b - a + 1));
                if (count > 0)
                    drops.push_back({"minecraft:" + name, count});
            }
        }
        for (const LootDrop& d : drops) {
            const CatalogItem* item = BlockCatalog::Instance().ItemByName(d.item);
            if (!item)
                continue;
            int left = d.count;
            const int limit = std::max(1, MaxStack(item->networkId));
            while (left > 0) {
                ItemStack stack;
                stack.networkId = item->networkId;
                stack.count = static_cast<uint16_t>(std::min(left, limit));
                left -= stack.count;
                server.SpawnItem(stack, m.x, m.y + 0.5, m.z, (rng() % 100 - 50) / 500.0, 0.2, (rng() % 100 - 50) / 500.0, 10);
            }
        }
    }
}
