#include "world/actor/ActorInternal.hpp"

namespace perlite {
    bool BlockSolid(uint16_t id) { return ShapeOf(id).solid; }

    bool BlockLiquid(uint16_t id) { return ShapeOf(id).liquid; }

    ActorManager::ActorManager(Server& s) : server(s) {}

    bool ActorManager::Solid(double x, double y, double z) const {
        const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)), bz = static_cast<int>(std::floor(z));
        if (by < kMinY)
            return true;
        if (!server.Chunks().Peek({bx >> 4, bz >> 4}))
            return true;
        return BlockSolid(server.Chunks().GetBlock(bx, by, bz));
    }

    bool ActorManager::Collides(const Mob& m, double x, double y, double z) const {
        const MobBody body = MobRegistry::Instance().BodyOf(m);
        const double hw = body.width * 0.5 - 0.001;
        const double h = body.height - 0.001;
        for (int bx = static_cast<int>(std::floor(x - hw)); bx <= static_cast<int>(std::floor(x + hw)); bx++)
            for (int bz = static_cast<int>(std::floor(z - hw)); bz <= static_cast<int>(std::floor(z + hw)); bz++)
                for (int by = static_cast<int>(std::floor(y)); by <= static_cast<int>(std::floor(y + h)); by++)
                    if (Solid(bx + 0.5, by + 0.5, bz + 0.5))
                        return true;
        return false;
    }

    int ActorManager::SurfaceY(int x, int z) const {
        const ChunkColumn* column = server.Chunks().Column(x >> 4, z >> 4);
        if (!column)
            return kMinY - 1;
        const int lx = x & 15, lz = z & 15;
        // Whole sections of one passable block (usually air) are skipped without looking at cells.
        for (int sec = kSections - 1; sec >= 0; sec--) {
            const ChunkColumn::Section& s = column->SectionAt(sec);
            const int base = kMinY + sec * 16;
            if (!s.cells) {
                if (BlockSolid(s.uniform) || BlockLiquid(s.uniform)) {
                    const int top = base + 15;
                    return top > kMinY ? top + 1 : kMinY - 1;
                }
                continue;
            }
            for (int y = base + 15; y >= base; y--) {
                if (y <= kMinY)
                    return kMinY - 1;
                const uint16_t b = (*s.cells)[static_cast<size_t>(CellIndex(lx, y - kMinY, lz))];
                if (BlockSolid(b) || BlockLiquid(b))
                    return y + 1;
            }
        }
        return kMinY - 1;
    }

    bool ActorManager::Day() const {
        const int64_t t = server.DayTime() % 24000;
        return t < 12500 || t > 23500;
    }

    bool ActorManager::Dark(int x, int y, int z) const {
        const bool sky = SurfaceY(x, z) <= y;
        if (sky)
            return !Day() || server.Thundering();
        return y < SurfaceY(x, z) - 4;
    }

    uint64_t ActorManager::Flags(const Mob& m) {
        namespace F = proto::FlagsMetadataFlags1;
        uint64_t f = F::HasCollision | F::Breathing | F::CanClimb | F::Walker;
        if (!(m.def->flags & mobflag::Fly) && !m.projectile && !((m.def->flags & mobflag::Aquatic) && m.inWater))
            f |= F::AffectedByGravity;
        if (m.burning)
            f |= F::Onfire;
        if (m.baby)
            f |= F::Baby;
        if (m.angry || m.target)
            f |= F::Angry;
        if (m.fuse >= 0)
            f |= F::Ignited;
        return f;
    }

    Mob* ActorManager::Spawn(const std::string& identifier, double x, double y, double z, bool persistent) {
        Mob m;
        if (identifier == "minecraft:arrow" || identifier == "arrow")
            return SpawnProjectile(projectile::Arrow, x, y, z, 0, 0, 0, 0, false);
        m.def = FindMob(identifier.c_str());
        if (!m.def)
            return nullptr;
        if (!m.projectile && server.HookEntitySpawn(m.def->id, nextId, x, y, z, !persistent))
            return nullptr;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.health = m.def->health;
        if (const MobType* type = MobRegistry::Instance().TypeOf(m.def)) {
            // Size classes (slime, magma cube): small, medium or large, each with its own box, health
            // and damage; the client draws the size from the variant.
            if (!type->sizes.empty()) {
                m.size = static_cast<int>(rng() % type->sizes.size());
                const VanillaSize& s = type->sizes[static_cast<size_t>(m.size)];
                m.variant = s.variant;
                if (s.health > 0)
                    m.health = s.health;
                m.damage = s.damage;
            } else if (!type->variants.empty()) {
                m.variant = type->variants[rng() % type->variants.size()];
            }
        }
        m.maxHealth = m.health;
        m.persistent = persistent;
        m.yaw = m.headYaw = static_cast<float>(rng() % 360);
        mobs.push_back(m);
        Mob& added = mobs.back();
        for (Session* s : server.PlayersIn(kOverworld))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    Mob* ActorManager::Find(uint64_t id) {
        for (Mob& m : mobs)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    bool ActorManager::Kill(uint64_t id) {
        Mob* m = Find(id);
        if (!m || m->deathTime > 0)
            return false;
        m->health = 0;
        Die(*m, nullptr);
        return true;
    }

    bool ActorManager::Remove(uint64_t id) {
        const auto it = std::ranges::find_if(mobs, [id](const Mob& m) { return m.id == id; });
        if (it == mobs.end())
            return false;
        for (Session* s : server.PlayersIn(kOverworld))
            s->HideItem(id);
        mobs.erase(it);
        return true;
    }

    bool ActorManager::Damage(uint64_t id, float amount) {
        Mob* m = Find(id);
        return m && Hurt(*m, amount, m->x, m->z, nullptr, 0.0f);
    }

    void ActorManager::Moved(Mob& m) {
        Broadcast(m, true);
        for (Session* s : server.PlayersIn(kOverworld))
            s->MobMotion(m);
    }

    int ActorManager::KillAll(const std::string& identifier) {
        int n = 0;
        for (Mob& m : mobs)
            if (m.deathTime == 0 && !m.projectile && m.kind == actorkind::Mob && (identifier.empty() || identifier == m.def->id ||
                                                      "minecraft:" + identifier == m.def->id)) {
                m.health = 0;
                Die(m, nullptr);
                n++;
            }
        return n;
    }

    void ActorManager::Show(Session& to) const {
        for (const Mob& m : mobs)
            if (m.deathTime == 0)
                to.ShowMob(m);
    }

    void ActorManager::Hide(Session& to) const {
        for (const Mob& m : mobs)
            to.HideItem(m.id);
    }

    Session* ActorManager::Nearest(const Mob& m, double range, bool survivalOnly) {
        Session* best = nullptr;
        double bestD = range * range;
        for (Session* s : server.PlayersIn(kOverworld)) {
            if (survivalOnly && (s->GameMode() == 1 || s->Spectator() || s->Dead()))
                continue;
            const double dx = s->X() - m.x, dy = (s->Y() - 1.62) - m.y, dz = s->Z() - m.z;
            const double d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        return best;
    }

    void ActorManager::Broadcast(Mob& m, bool force) {
        const bool moved = std::abs(static_cast<float>(m.x) - m.lastX) > 0.005f || std::abs(static_cast<float>(m.y) - m.lastY) > 0.005f ||
                           std::abs(static_cast<float>(m.z) - m.lastZ) > 0.005f || Angle(m.yaw) != Angle(m.lastYaw) ||
                           Angle(m.headYaw) != Angle(m.lastHead);
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        if (moved || force) {
            for (Session* s : players)
                s->MoveMob(m);
            m.lastX = static_cast<float>(m.x);
            m.lastY = static_cast<float>(m.y);
            m.lastZ = static_cast<float>(m.z);
            m.lastYaw = m.yaw;
            m.lastHead = m.headYaw;
        }
        const uint64_t flags = Flags(m);
        if (flags != m.lastFlags || force) {
            for (Session* s : players)
                s->MobData(m);
            m.lastFlags = flags;
        }
    }

    void ActorManager::Tick() {
        if (++spawnTick % 20 == 0)
            NaturalSpawns();
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        for (auto it = mobs.begin(); it != mobs.end();) {
            Mob& m = *it;
            m.age++;
            bool remove = false;
            if (m.deathTime > 0) {
                remove = ++m.deathTime > 20;
            } else if (m.projectile && m.age > 1200 && m.kind == actorkind::Mob) {
                remove = true;
            } else if (server.Chunks().Peek({static_cast<int>(std::floor(m.x)) >> 4, static_cast<int>(std::floor(m.z)) >> 4})) {
                if (!m.persistent && !m.projectile) {
                    double nearest = 1e18;
                    for (Session* s : players) {
                        const double dx = s->X() - m.x, dz = s->Z() - m.z, dy = s->Y() - m.y;
                        nearest = std::min(nearest, dx * dx + dy * dy + dz * dz);
                    }
                    remove = nearest > 128.0 * 128.0 || (nearest > 32.0 * 32.0 && rng() % 800 == 0);
                }
                if (!remove && m.kind != actorkind::Mob) {
                    remove = TickSpecial(m);
                    if (!remove)
                        Broadcast(m, false);
                } else if (!remove) {
                    if (m.hurtTime > 0)
                        m.hurtTime--;
                    if (m.invulnerable > 0)
                        m.invulnerable--;
                    if (m.lastAttackerTicks > 0)
                        m.lastAttackerTicks--;
                    Think(m);
                    if (m.deathTime == 0) {
                        if (m.health <= 0)
                            Die(m, nullptr);
                        else {
                            Physics(m);
                            Broadcast(m, false);
                        }
                    }
                }
            }
            if (remove) {
                for (Session* s : players)
                    s->HideItem(m.id);
                it = mobs.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool ActorManager::TickSpecial(Mob& m) {
        switch (m.kind) {
        case actorkind::FallingBlock:
            return TickFallingBlock(m);
        case actorkind::PrimedTnt:
            return TickPrimedTnt(m);
        case actorkind::ExperienceOrb:
            return TickExperienceOrb(m);
        case actorkind::Projectile:
            return TickShot(m);
        default:
            return true;
        }
    }
}
