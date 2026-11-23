#include "world/actor/ActorInternal.hpp"
#include <cstdint>

namespace perlite {
    bool BlockSolid(uint16_t id) { return ShapeOf(id).solid; }

    bool BlockLiquid(uint16_t id) { return ShapeOf(id).liquid; }

    // Entity ids of each dimension come from their own range (all above player ids).
    ActorManager::ActorManager(Server& s, int dim)
        : server(s), dimension(dim), nextId(5'000'000 + static_cast<uint64_t>(dim) * 1'000'000'000'000ull) {}

    ChunkManager& ActorManager::World() const {
        return server.World(dimension);
    }

    bool ActorManager::Solid(double x, double y, double z) const {
        const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)), bz = static_cast<int>(std::floor(z));
        if (by < kMinY)
            return true;
        if (!World().Peek({bx >> 4, bz >> 4}))
            return true;
        return BlockSolid(World().GetBlock(bx, by, bz));
    }

    bool ActorManager::CanSee(const Mob& m, double x, double y, double z) const {
        const double ey = m.y + MobRegistry::Instance().BodyOf(m).height * 0.85;
        const double dx = x - m.x, dy = y - ey, dz = z - m.z;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > 128.0)
            return false;
        const int steps = static_cast<int>(len / 0.25) + 1;
        int lx = INT32_MIN, ly = INT32_MIN, lz = INT32_MIN;
        for (int i = 1; i < steps; i++) {
            const double t = static_cast<double>(i) / steps;
            const int bx = static_cast<int>(std::floor(m.x + dx * t)), by = static_cast<int>(std::floor(ey + dy * t)),
                      bz = static_cast<int>(std::floor(m.z + dz * t));
            if (bx == lx && by == ly && bz == lz)
                continue;
            lx = bx, ly = by, lz = bz;
            if (!World().Peek({bx >> 4, bz >> 4}))
                return false;
            const uint16_t b = World().GetBlock(bx, by, bz);
            if (BlockSolid(b)) {
                // Glass, leaves and the like do not block sight in the game.
                const std::string n = BlockRegistry::Instance().Name(b);
                if (!n.contains("glass") && !n.contains("leaves") && !n.contains("fence") && !n.contains("bars") &&
                    !n.contains("slab") && !n.contains("stairs"))
                    return false;
            }
        }
        return true;
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
        const ChunkColumn* column = World().Column(x >> 4, z >> 4);
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
        if (!(m.def->flags & mobflag::Fly) && !m.projectile && !m.floats && !((m.def->flags & mobflag::Aquatic) && m.inWater))
            f |= F::AffectedByGravity;
        if (m.burning)
            f |= F::Onfire;
        if (m.baby)
            f |= F::Baby;
        if (m.angry || m.target)
            f |= F::Angry;
        if (m.fuse >= 0)
            f |= F::Ignited;
        if (m.kind == actorkind::Projectile && m.crit && !m.stuck)
            f |= F::Critical;
        if (m.kind == actorkind::Crystal && m.showBase)
            f |= F::Showbase;
        if (m.sitting)
            f |= F::Sitting;
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
        if (std::string_view(m.def->id) == "minecraft:ender_dragon") {
            // The dragon flies its own way (see boss/EnderDragon.cpp) and never despawns.
            m.dragon = true;
            m.floats = true;
            m.persistent = true;
        }
        // What the mob holds, as vanilla equips it on spawn.
        {
            const std::string_view id = m.def->id;
            const char* item = nullptr;
            if (id == "minecraft:skeleton" || id == "minecraft:stray" || id == "minecraft:bogged" ||
                id == "minecraft:parched")
                item = "minecraft:bow";
            else if (id == "minecraft:wither_skeleton")
                item = "minecraft:stone_sword";
            else if (id == "minecraft:pillager")
                item = "minecraft:crossbow";
            else if (id == "minecraft:piglin")
                item = rng() % 2 ? "minecraft:golden_sword" : "minecraft:crossbow";
            else if (id == "minecraft:zombie_pigman" || id == "minecraft:zombified_piglin")
                item = "minecraft:golden_sword";
            else if (id == "minecraft:piglin_brute")
                item = "minecraft:golden_axe";
            else if (id == "minecraft:vindicator")
                item = "minecraft:iron_axe";
            else if (id == "minecraft:vex")
                item = "minecraft:iron_sword";
            else if (id == "minecraft:drowned" && rng() % 16 == 0)
                item = "minecraft:trident";
            else if (id == "minecraft:fox" && rng() % 5 == 0)
                item = "minecraft:sweet_berries";
            if (item)
                if (const CatalogItem* c = BlockCatalog::Instance().ItemByName(item))
                    m.held = c->networkId;
        }
        m.yaw = m.headYaw = static_cast<float>(rng() % 360);
        Mob& added = Add(m);
        for (Session* s : server.PlayersIn(dimension))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    Mob* ActorManager::Find(uint64_t id) {
        const auto it = byId.find(id);
        return it == byId.end() ? nullptr : it->second;
    }

    Mob& ActorManager::Add(const Mob& m) {
        mobs.push_back(m);
        Mob& added = mobs.back();
        byId[added.id] = &added;
        return added;
    }

    std::list<Mob>::iterator ActorManager::Erase(std::list<Mob>::iterator it) {
        byId.erase(it->id);
        if (it->inGrid)
            if (const auto cell = grid.find(it->gridCell); cell != grid.end())
                std::erase(cell->second, &*it);
        return mobs.erase(it);
    }

    void ActorManager::RebuildGrid() {
        for (auto& [cell, list] : grid)
            list.clear();
        for (Mob& m : mobs) {
            m.inGrid = false;
            if (m.deathTime != 0)
                continue;
            const int64_t cell = (static_cast<int64_t>(static_cast<int>(std::floor(m.x)) >> 3) << 32) ^
                                 static_cast<int64_t>(static_cast<uint32_t>(static_cast<int>(std::floor(m.z)) >> 3));
            grid[cell].push_back(&m);
            m.inGrid = true;
            m.gridCell = cell;
        }
        if (grid.size() > 4 * mobs.size() + 1024)
            std::erase_if(grid, [](const auto& e) { return e.second.empty(); });
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
        for (Session* s : server.PlayersIn(dimension))
            s->HideItem(id);
        Erase(it);
        return true;
    }

    bool ActorManager::Damage(uint64_t id, float amount) {
        Mob* m = Find(id);
        return m && Hurt(*m, amount, m->x, m->z, nullptr, 0.0f);
    }

    void ActorManager::Moved(Mob& m) {
        Broadcast(m, true);
        for (Session* s : server.PlayersIn(dimension))
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
        for (Session* s : server.PlayersIn(dimension)) {
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
        std::vector<Session*> fresh;
        if (!ticking)
            fresh = server.PlayersIn(dimension);
        const std::vector<Session*>& players = ticking ? tickPlayers : fresh;
        if (moved || force) {
            const Session::Encoded move = Session::EncodeMoveMob(m);
            for (Session* s : players)
                s->SendEncoded(move);
            m.lastX = static_cast<float>(m.x);
            m.lastY = static_cast<float>(m.y);
            m.lastZ = static_cast<float>(m.z);
            m.lastYaw = m.yaw;
            m.lastHead = m.headYaw;
        }
        const uint64_t flags = Flags(m);
        if (flags != m.lastFlags || m.target != m.lastTarget || force) {
            for (Session* s : players)
                s->MobData(m);
            m.lastFlags = flags;
            m.lastTarget = m.target;
        }
    }

    void ActorManager::Tick() {
        effectsThisTick = 0;
        fuseSoundsThisTick = 0;
        tickPlayers = server.PlayersIn(dimension);
        ticking = true;
        RebuildGrid();
        if (++spawnTick % 20 == 0)
            NaturalSpawns();
        const std::vector<Session*> players = server.PlayersIn(dimension);
        for (auto it = mobs.begin(); it != mobs.end();) {
            Mob& m = *it;
            m.age++;
            bool remove = false;
            if (m.deathTime > 0) {
                remove = ++m.deathTime > 20;
            } else if (m.projectile && m.age > 1200 && m.kind == actorkind::Mob) {
                remove = true;
            } else if (World().Peek({static_cast<int>(std::floor(m.x)) >> 4, static_cast<int>(std::floor(m.z)) >> 4})) {
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
                    if (!remove) {
                        // Primed TNT and falling blocks fly on the client from their motion; their
                        // position is corrected every few ticks, and the motion sent when an
                        // explosion pushes them.
                        const bool ballistic = m.kind == actorkind::PrimedTnt || m.kind == actorkind::FallingBlock;
                        if (!ballistic || (m.age + static_cast<int>(m.id)) % 5 == 0)
                            Broadcast(m, false);
                        if (m.kicked) {
                            const Session::Encoded motion = Session::EncodeMobMotion(m, server.TickCount());
                            for (Session* s : players)
                                s->SendEncoded(motion);
                            m.kicked = false;
                        }
                    }
                } else if (!remove) {
                    if (m.hurtTime > 0)
                        m.hurtTime--;
                    if (m.invulnerable > 0)
                        m.invulnerable--;
                    if (m.lastAttackerTicks > 0)
                        m.lastAttackerTicks--;
                    if (IsDragon(m)) {
                        TickDragon(m);
                        if (m.deathTime == 0)
                            Broadcast(m, false);
                    } else
                        Think(m);
                    if (m.deathTime == 0 && !IsDragon(m)) {
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
                it = Erase(it);
            } else {
                ++it;
            }
        }
        TickClouds();
        RunBlasts();
        ticking = false;
        tickPlayers.clear();
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
        case actorkind::Crystal:
            if (m.showBase && dimension == kEnd && m.age % 20 == 0) {
                const int bx = static_cast<int>(std::floor(m.x)), by = static_cast<int>(std::floor(m.y)), bz = static_cast<int>(std::floor(m.z));
                if (World().GetBlock(bx, by, bz) == BlockRegistry::kAir)
                    server.SetBlockIn(dimension, bx, by, bz, BlockCatalog::Instance().DefaultBlock("minecraft:fire"));
            }
            return false;
        default:
            return true;
        }
    }
}
