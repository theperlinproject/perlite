// The ender dragon and the end crystals. The dragon works through the game's phases: it circles
// the island on a ring of path nodes, now and then swoops at a player to spit a fireball, lands on
// the exit portal to breathe fire, takes off again, and charges players it sees from the podium.
// Crystals within 32 blocks heal it; destroying the one healing it hurts it. When it dies it rises
// for ten seconds and bursts into experience.
#include "world/actor/ActorInternal.hpp"
#include "world/level/end/DragonFight.hpp"

namespace perlite {
    namespace {
        const MobDef* CrystalDef() {
            static const MobDef crystal{"minecraft:ender_crystal", 2.0f, 2.0f, 1, 0, 0, 0, "", "", ""};
            return &crystal;
        }

        // Blocks the dragon does not break (the game's dragon_immune tag).
        bool DragonImmune(uint16_t id) {
            static std::atomic<int8_t> cache[0x10000] = {};
            if (const int8_t known = cache[id].load(std::memory_order_relaxed))
                return known == 2;
            const std::string& n = BlockRegistry::Instance().Name(id);
            static const char* const kImmune[] = {"bedrock", "end_stone", "obsidian", "crying_obsidian", "iron_bars",
                                                  "end_portal", "end_gateway", "barrier", "command_block", "respawn_anchor",
                                                  "structure_block", "jigsaw", "moving_block", "light_block", "reinforced_deepslate",
                                                  "end_portal_frame", "border_block", "allow", "deny", "fire", "dragon_egg"};
            bool immune = id == BlockRegistry::kAir || BlockLiquid(id);
            for (const char* k : kImmune)
                immune = immune || n.contains(k);
            cache[id].store(immune ? 2 : 1, std::memory_order_relaxed);
            return immune;
        }

        void Forward(const Mob& m, double& fx, double& fz) {
            const double yaw = m.yaw * kPi / 180.0;
            fx = -std::sin(yaw);
            fz = std::cos(yaw);
        }
    }

    Mob* ActorManager::SpawnCrystal(double x, double y, double z, bool showBase) {
        Mob m;
        m.def = CrystalDef();
        m.kind = actorkind::Crystal;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.health = 1;
        m.persistent = true;
        m.showBase = showBase;
        m.floats = true;
        Mob& added = Add(m);
        for (Session* s : server.PlayersIn(dimension))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    Mob* ActorManager::SpawnDragon(double x, double y, double z) {
        Mob* m = Spawn("minecraft:ender_dragon", x, y, z, true);
        if (!m)
            return nullptr;
        m->floats = true;
        m->phase = dragonphase::Holding;
        m->node = -1;
        m->yaw = m->headYaw = static_cast<float>(rng() % 360);
        return m;
    }

    void ActorManager::SetBeam(Mob& crystal, bool on, int x, int y, int z) {
        if (crystal.beam == on && (!on || (crystal.beamX == x && crystal.beamY == y && crystal.beamZ == z)))
            return;
        crystal.beam = on;
        crystal.beamX = x;
        crystal.beamY = y;
        crystal.beamZ = z;
        for (Session* s : server.PlayersIn(dimension))
            s->MobData(crystal);
    }

    void ActorManager::QueueExplosion(double x, double y, double z, float power, uint64_t source, bool breakBlocks) {
        if (server.HookExplosion(x, y, z, power, source) || power <= 0)
            return;
        pendingBlasts.push_back(MakeBlast(x, y, z, power, source, breakBlocks));
    }

    void ActorManager::DestroyCrystal(Mob& c, Session* by) {
        if (c.deathTime != 0)
            return;
        c.deathTime = 19;
        c.health = 0;
        for (Session* s : server.PlayersIn(dimension))
            s->HideItem(c.id);
        // A crystal blown up next to the dragon healing from it hurts the dragon; its attacker is
        // who the dragon goes after.
        for (Mob& d : mobs)
            if (IsDragon(d) && d.deathTime == 0 && d.healer == c.id) {
                d.healer = 0;
                DragonHurt(d, 10.0f, d.x, d.z, nullptr);
                if (by && d.phase == dragonphase::Holding && !by->Creative()) {
                    d.phase = dragonphase::Strafe;
                    d.phaseTicks = 0;
                    d.target = by->Id();
                    d.flames = 0;
                }
            }
        if (dimension == kEnd)
            server.Fight().CrystalDestroyed(c.id);
        QueueExplosion(c.x, c.y, c.z, 6.0f, c.id, true);
    }

    void ActorManager::BreathCloud(double x, double y, double z, float radius, int duration, float growth, uint64_t owner) {
        Cloud c;
        c.x = x;
        c.y = y;
        c.z = z;
        c.radius = radius;
        c.duration = duration;
        c.growth = growth;
        c.owner = owner;
        clouds.push_back(c);
    }

    void ActorManager::TickClouds() {
        if (clouds.empty())
            return;
        const std::vector<Session*> players = server.PlayersIn(dimension);
        for (auto it = clouds.begin(); it != clouds.end();) {
            Cloud& c = *it;
            c.ticks++;
            c.radius += c.growth;
            if (c.ticks >= c.duration || c.radius < 0.5f) {
                it = clouds.erase(it);
                continue;
            }
            if (c.ticks % 4 == 0)
                for (Session* s : players) {
                    if (std::abs(s->X() - c.x) > 64 || std::abs(s->Z() - c.z) > 64)
                        continue;
                    const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
                    const double r = c.radius * std::sqrt(std::uniform_real_distribution<double>(0, 1)(rng));
                    s->SpawnParticle("minecraft:dragon_breath_lingering", c.x + std::cos(a) * r, c.y + 0.2, c.z + std::sin(a) * r);
                }
            // The breath carries Instant Damage II: 6 health every second to what stands in it.
            if (c.ticks % 20 == 10) {
                for (Session* s : players) {
                    const double dx = s->X() - c.x, dz = s->Z() - c.z, dy = s->Y() - 1.62 - c.y;
                    if (dx * dx + dz * dz <= c.radius * c.radius && dy > -1.0 && dy < 3.0 && !s->Creative() && !s->Spectator())
                        s->Damage(6.0f, Session::DamageCause::Magic, c.x, c.z, "minecraft:ender_dragon");
                }
                ForNear(c.x, c.z, c.radius, [&](Mob& o) {
                    if (o.kind != actorkind::Mob || o.deathTime || IsDragon(o) || o.projectile)
                        return;
                    const double dx = o.x - c.x, dz = o.z - c.z;
                    if (dx * dx + dz * dz <= c.radius * c.radius && std::abs(o.y - c.y) < 3.0)
                        Hurt(o, 6.0f, c.x, c.z, nullptr, 0.0f);
                });
            }
            ++it;
        }
    }

    void ActorManager::DragonHurt(Mob& m, float damage, double fromX, double fromZ, Session* by) {
        if (m.phase == dragonphase::Dying || m.deathTime || damage <= 0)
            return;
        if (server.HookEntityDamage(m, damage, by) || damage <= 0)
            return;
        // Only its head takes the full blow; the body and wings take a quarter (plus one).
        double fx, fz;
        Forward(m, fx, fz);
        const double hx = m.x + fx * 6.5, hz = m.z + fz * 6.5;
        if (std::hypot(fromX - hx, fromZ - hz) > std::hypot(fromX - m.x, fromZ - m.z))
            damage = damage / 4.0f + std::min(damage, 1.0f);
        float dealt = damage;
        if (m.invulnerable > 10) {
            if (damage <= m.lastHurt)
                return;
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
        for (Session* s : server.PlayersIn(dimension))
            s->MobEvent(m.id, 2, 0);
        if (m.sitting) {
            // Enough damage on the podium sends it back up.
            m.sittingDamage += dealt;
            if (m.sittingDamage > 0.25f * m.maxHealth) {
                m.sittingDamage = 0;
                m.phase = dragonphase::Takeoff;
                m.phaseTicks = 0;
                m.sitting = false;
            }
        }
        if (m.health <= 0) {
            m.health = 1;
            m.phase = dragonphase::Dying;
            m.phaseTicks = 0;
            m.sitting = false;
            m.vx = m.vy = m.vz = 0;
            for (Session* s : server.PlayersIn(dimension))
                s->MobEvent(m.id, 37, 0); // the death animation
        }
    }

    void ActorManager::DragonFlyTo(Mob& m, double tx, double ty, double tz, double speed) {
        const double dx = tx - m.x, dy = ty - m.y, dz = tz - m.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        double wx = 0, wy = 0, wz = 0;
        if (d > 1e-6) {
            const double s = std::min(speed, d);
            wx = dx / d * s;
            wy = dy / d * s;
            wz = dz / d * s;
        }
        // It turns and climbs gradually, like the game's dragon steering.
        m.vx += (wx - m.vx) * 0.08;
        m.vy += (wy - m.vy) * 0.1;
        m.vz += (wz - m.vz) * 0.08;
        m.x += m.vx;
        m.y += m.vy;
        m.z += m.vz;
        const double flat = std::sqrt(m.vx * m.vx + m.vz * m.vz);
        if (flat > 0.02)
            m.yaw = m.headYaw = static_cast<float>(std::atan2(-m.vx, m.vz) * 180.0 / kPi);
        m.pitch = static_cast<float>(std::clamp(-std::atan2(m.vy, std::max(flat, 0.01)) * 180.0 / kPi, -45.0, 45.0));
    }

    void ActorManager::DragonHoldingTarget(Mob& m) {
        DragonFight& fight = server.Fight();
        const int crystals = fight.CrystalsAlive();
        if (rng() % static_cast<unsigned>(crystals + 3) == 0) {
            m.phase = dragonphase::LandingApproach;
            m.phaseTicks = 0;
            return;
        }
        if (Session* p = Nearest(m, 64.0, true)) {
            const double px = p->X(), pz = p->Z(), py = p->Y() - 1.62 - fight.PortalY();
            const double fromPodium = (px * px + py * py + pz * pz) / 512.0;
            if (rng() % static_cast<unsigned>(static_cast<int>(std::abs(fromPodium)) + 2) == 0 ||
                rng() % static_cast<unsigned>(crystals + 2) == 0) {
                m.phase = dragonphase::Strafe;
                m.phaseTicks = 0;
                m.target = p->Id();
                m.flames = 0;
                return;
            }
        }
        // On to the next node of its ring: the outer ring while crystals stand, the middle one
        // after; now and then it turns round.
        const bool outer = crystals > 0;
        const int first = outer ? 0 : 12, count = outer ? 12 : 8;
        if (rng() % 8 == 0)
            m.clockwise = !m.clockwise;
        if (m.node < first || m.node >= first + count) {
            int best = first;
            double bestD = 1e300;
            for (int i = first; i < first + count; i++) {
                const auto n = fight.Node(i);
                const double d = (n[0] - m.x) * (n[0] - m.x) + (n[2] - m.z) * (n[2] - m.z);
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            m.node = best;
        } else {
            m.node = first + (m.node - first + (m.clockwise ? count - 1 : 1)) % count;
        }
    }

    void ActorManager::DragonContact(Mob& m) {
        if (m.hurtTime > 0)
            return;
        double fx, fz;
        Forward(m, fx, fz);
        const double hx = m.x + fx * 6.5, hy = m.y + (m.sitting ? 0.5 : 1.5), hz = m.z + fz * 6.5;
        for (Session* s : tickPlayers) {
            if (s->Creative() || s->Spectator() || s->Dead())
                continue;
            const double feet = s->Y() - 1.62;
            // The head bites.
            if (std::abs(s->X() - hx) < 2.0 && std::abs(s->Z() - hz) < 2.0 && feet > hy - 2.8 && feet < hy + 1.5) {
                s->Damage(10.0f, Session::DamageCause::Mob, hx, hz, "minecraft:ender_dragon");
                continue;
            }
            // The wings and body knock players away (and hurt them in flight).
            const double dx = s->X() - m.x, dz = s->Z() - m.z;
            if (std::abs(dx) < 6.5 && std::abs(dz) < 6.5 && feet > m.y - 3.0 && feet < m.y + 4.0) {
                const double d = std::max(dx * dx + dz * dz, 0.1);
                s->SetVelocity(dx / d * 4.0, 0.2, dz / d * 4.0);
                if (!m.sitting)
                    s->Damage(5.0f, Session::DamageCause::Mob, m.x, m.z, "minecraft:ender_dragon");
            }
        }
    }

    void ActorManager::DragonBreakBlocks(Mob& m) {
        if (m.sitting || m.phase == dragonphase::Dying)
            return;
        double fx, fz;
        Forward(m, fx, fz);
        const bool griefing = server.RuleValue("mobgriefing");
        bool inWall = false, broke = false;
        const auto box = [&](double cx, double cy, double cz, double hw, double h) {
            for (int x = static_cast<int>(std::floor(cx - hw)); x <= static_cast<int>(std::floor(cx + hw)); x++)
                for (int z = static_cast<int>(std::floor(cz - hw)); z <= static_cast<int>(std::floor(cz + hw)); z++)
                    for (int y = static_cast<int>(std::floor(cy)); y <= static_cast<int>(std::floor(cy + h)); y++) {
                        if (!World().Peek({x >> 4, z >> 4}))
                            continue;
                        const uint16_t b = World().GetBlock(x, y, z);
                        if (b == BlockRegistry::kAir)
                            continue;
                        if (DragonImmune(b)) {
                            inWall = inWall || BlockSolid(b);
                            continue;
                        }
                        if (griefing) {
                            server.SetBlockIn(dimension, x, y, z, BlockRegistry::kAir);
                            broke = true;
                        } else {
                            inWall = true;
                        }
                    }
        };
        box(m.x, m.y, m.z, 2.5, 3.0);
        box(m.x + fx * 6.5, m.y + 1.0, m.z + fz * 6.5, 1.0, 1.0);
        if (broke)
            for (Session* s : tickPlayers)
                s->SendLevelEvent(16390, m.x + fx * 3.0, m.y + 1.0, m.z + fz * 3.0, 0);
        if (inWall) {
            m.vx *= 0.8;
            m.vy *= 0.8;
            m.vz *= 0.8;
        }
    }

    void ActorManager::TickDragon(Mob& m) {
        DragonFight& fight = server.Fight();
        m.phaseTicks++;
        if (m.hurtTime > 0)
            m.hurtTime--;
        if (m.invulnerable > 0)
            m.invulnerable--;
        if (m.maxHealth <= 0)
            m.maxHealth = m.def->health;
        const double podiumY = fight.PortalY() > 0 ? fight.PortalY() : 64.0;

        if (m.phase == dragonphase::Dying) {
            // Ten seconds of rising and bursting, the experience in the last two and a half.
            m.y += 0.1;
            m.yaw += 20.0f;
            m.headYaw = m.yaw;
            const int total = fight.PreviouslyKilled() ? 500 : 12000;
            if (m.phaseTicks >= 180 && m.phaseTicks <= 200)
                for (Session* s : tickPlayers)
                    s->SpawnParticle("minecraft:dragon_death_explosion_emitter",
                                     m.x + (static_cast<double>(rng() % 800) / 100.0 - 4.0),
                                     m.y + 2.0 + (static_cast<double>(rng() % 400) / 100.0 - 2.0),
                                     m.z + (static_cast<double>(rng() % 800) / 100.0 - 4.0));
            if (m.phaseTicks > 150 && m.phaseTicks % 5 == 0)
                SpawnXpOrbs(m.x, m.y, m.z, static_cast<int>(std::floor(total * 0.08)));
            if (m.phaseTicks == 1)
                for (Session* s : tickPlayers)
                    s->PlaySoundTo("mob.enderdragon.death", m.x, m.y, m.z, 4.0f, 1.0f);
            if (m.phaseTicks >= 200) {
                SpawnXpOrbs(m.x, m.y, m.z, static_cast<int>(std::floor(total * 0.2)));
                server.HookEntityDeath(m, nullptr);
                m.health = 0;
                m.deathTime = 19;
                if (dimension == kEnd)
                    fight.DragonKilled(m.id);
            }
            return;
        }

        // Healing from the nearest crystal within 32 blocks, a point every half second.
        if (m.healer) {
            Mob* c = Find(m.healer);
            if (!c || c->deathTime) {
                m.healer = 0;
            } else {
                if (m.age % 10 == 0 && m.health < m.maxHealth)
                    m.health = std::min(m.maxHealth, m.health + 1.0f);
                if (m.age % 10 == 0)
                    SetBeam(*c, true, static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)), static_cast<int>(std::floor(m.z)));
            }
        }
        if (rng() % 10 == 0) {
            Mob* best = nullptr;
            double bestD = 1e300;
            ForNear(m.x, m.z, 32.0, [&](Mob& o) {
                if (o.kind != actorkind::Crystal || o.deathTime)
                    return;
                if (std::abs(o.x - m.x) > 32 + 6.5 || std::abs(o.z - m.z) > 32 + 6.5 || std::abs(o.y - m.y) > 32 + 4)
                    return;
                const double d = (o.x - m.x) * (o.x - m.x) + (o.y - m.y) * (o.y - m.y) + (o.z - m.z) * (o.z - m.z);
                if (d < bestD) {
                    bestD = d;
                    best = &o;
                }
            });
            const uint64_t next = best ? best->id : 0;
            if (next != m.healer) {
                if (Mob* old = m.healer ? Find(m.healer) : nullptr)
                    SetBeam(*old, false);
                m.healer = next;
            }
        }

        Session* target = m.target ? server.FindSession(m.target) : nullptr;
        if (target && (target->DimensionId() != dimension || target->Dead() || target->Creative() || target->Spectator()))
            target = nullptr;
        m.sitting = m.phase == dragonphase::Scanning || m.phase == dragonphase::Roaring || m.phase == dragonphase::Flaming;

        switch (m.phase) {
        case dragonphase::Holding: {
            if (m.node < 0)
                DragonHoldingTarget(m);
            if (m.phase != dragonphase::Holding)
                break;
            const auto n = fight.Node(m.node);
            DragonFlyTo(m, n[0], n[1], n[2], 0.9);
            const double d2 = (n[0] - m.x) * (n[0] - m.x) + (n[1] - m.y) * (n[1] - m.y) + (n[2] - m.z) * (n[2] - m.z);
            if (d2 < 100.0 || d2 > 22500.0 || m.phaseTicks > 600) {
                m.phaseTicks = 0;
                DragonHoldingTarget(m);
            }
            break;
        }
        case dragonphase::Strafe: {
            if (!target || m.phaseTicks > 400) {
                m.phase = dragonphase::Holding;
                m.phaseTicks = 0;
                m.node = -1;
                break;
            }
            const double tx = target->X(), tz = target->Z(), ty = target->Y() - 1.62;
            const double flat = std::hypot(tx - m.x, tz - m.z);
            const double lift = std::min(0.4 + flat / 80.0 - 1.0, 10.0);
            DragonFlyTo(m, tx, ty + lift + 6.0, tz, 0.9);
            const double dx = tx - m.x, dy = ty + 1.0 - m.y, dz = tz - m.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < 4096.0) {
                m.flames++;
                double fx, fz;
                Forward(m, fx, fz);
                const double len = std::max(1e-6, std::hypot(dx, dz));
                const double facing = (fx * dx + fz * dz) / len;
                if (m.flames >= 5 && facing > 0.94) {
                    // A fireball from the mouth, straight at the player.
                    const double hx = m.x + fx * 6.5, hy = m.y + 1.0, hz = m.z + fz * 6.5;
                    const double ax = tx - hx, ay = ty + 0.9 - hy, az = tz - hz;
                    const double al = std::max(1e-6, std::sqrt(ax * ax + ay * ay + az * az));
                    SpawnProjectile(projectile::DragonFireball, hx, hy, hz, ax / al * 1.5, ay / al * 1.5, az / al * 1.5, m.id, false);
                    for (Session* s : tickPlayers)
                        s->PlaySoundTo("mob.enderdragon.shoot", hx, hy, hz, 4.0f, 1.0f);
                    m.flames = 0;
                    m.phase = dragonphase::Holding;
                    m.phaseTicks = 0;
                    m.node = -1;
                }
            } else if (m.flames > 0) {
                m.flames--;
            }
            break;
        }
        case dragonphase::LandingApproach: {
            DragonFlyTo(m, 0.5, podiumY + 20.0, 0.5, 0.9);
            if (std::hypot(m.x - 0.5, m.z - 0.5) < 12.0 || m.phaseTicks > 600) {
                m.phase = dragonphase::Landing;
                m.phaseTicks = 0;
            }
            break;
        }
        case dragonphase::Landing: {
            DragonFlyTo(m, 0.5, podiumY + 4.0, 0.5, 0.5);
            const double d2 = (m.x - 0.5) * (m.x - 0.5) + (m.y - podiumY - 4.0) * (m.y - podiumY - 4.0) + (m.z - 0.5) * (m.z - 0.5);
            if (d2 < 1.5 || m.phaseTicks > 300) {
                m.x = 0.5;
                m.y = podiumY + 4.0;
                m.z = 0.5;
                m.vx = m.vy = m.vz = 0;
                m.phase = dragonphase::Scanning;
                m.phaseTicks = 0;
                m.flames = 0;
                m.sittingDamage = 0;
            }
            break;
        }
        case dragonphase::Scanning: {
            // Turns to the nearest player within 20 blocks and breathes at them; with nobody close
            // for five seconds it takes off, charging at someone up to 150 blocks away.
            Session* near = Nearest(m, 20.0, true);
            if (near) {
                const float want = static_cast<float>(std::atan2(-(near->X() - m.x), near->Z() - m.z) * 180.0 / kPi);
                float diff = std::remainder(want - m.yaw, 360.0f);
                m.yaw += std::clamp(diff, -10.0f, 10.0f);
                m.headYaw = m.yaw;
                if (m.phaseTicks > 25) {
                    m.phase = dragonphase::Roaring;
                    m.phaseTicks = 0;
                    for (Session* s : tickPlayers)
                        s->PlaySoundTo("mob.enderdragon.growl", m.x, m.y, m.z, 4.0f, 1.0f);
                }
            } else if (m.phaseTicks >= 100) {
                if (Session* far = Nearest(m, 150.0, true)) {
                    m.phase = dragonphase::Charging;
                    m.goalX = far->X();
                    m.goalY = far->Y() - 1.62;
                    m.goalZ = far->Z();
                } else {
                    m.phase = dragonphase::Takeoff;
                }
                m.phaseTicks = 0;
            }
            break;
        }
        case dragonphase::Roaring:
            if (m.phaseTicks >= 40) {
                m.phase = dragonphase::Flaming;
                m.phaseTicks = 0;
                m.flames++;
            }
            break;
        case dragonphase::Flaming: {
            // Ten seconds of breath on the ground in front of it.
            if (m.phaseTicks == 10) {
                double fx, fz;
                Forward(m, fx, fz);
                const double bx = m.x + fx * 6.5, bz = m.z + fz * 6.5;
                int by = static_cast<int>(std::floor(m.y));
                while (by > 0 && !BlockSolid(World().GetBlock(static_cast<int>(std::floor(bx)), by, static_cast<int>(std::floor(bz)))))
                    by--;
                BreathCloud(bx, by + 1.0, bz, 5.0f, 200, 0.0f, m.id);
            }
            if (m.phaseTicks >= 10 && m.phaseTicks % 10 == 0)
                for (Session* s : tickPlayers)
                    s->MobEvent(m.id, 68, 0); // the breath (DragonPuke)
            if (m.phaseTicks >= 200) {
                m.phase = m.flames >= 4 ? dragonphase::Takeoff : dragonphase::Scanning;
                m.phaseTicks = 0;
                if (m.phase == dragonphase::Takeoff)
                    m.flames = 0;
            }
            break;
        }
        case dragonphase::Takeoff: {
            double fx, fz;
            Forward(m, fx, fz);
            if (m.phaseTicks == 1) {
                m.goalX = 0.5 - fx * 40.0;
                m.goalZ = 0.5 - fz * 40.0;
                m.goalY = podiumY + 40.0;
            }
            DragonFlyTo(m, m.goalX, m.goalY, m.goalZ, 0.9);
            if (std::abs(m.y - m.goalY) < 6.0 || m.phaseTicks > 300) {
                m.phase = dragonphase::Holding;
                m.phaseTicks = 0;
                m.node = -1;
            }
            break;
        }
        case dragonphase::Charging: {
            DragonFlyTo(m, m.goalX, m.goalY, m.goalZ, 1.5);
            const double d2 = (m.goalX - m.x) * (m.goalX - m.x) + (m.goalY - m.y) * (m.goalY - m.y) + (m.goalZ - m.z) * (m.goalZ - m.z);
            if (d2 < 100.0 || d2 > 22500.0 || m.phaseTicks > 200) {
                m.phase = dragonphase::Holding;
                m.phaseTicks = 0;
                m.node = -1;
            }
            break;
        }
        default:
            m.phase = dragonphase::Holding;
            m.node = -1;
            break;
        }
        DragonContact(m);
        DragonBreakBlocks(m);
        // Fell out of the world (pushed by something): back above the island.
        if (m.y < -40) {
            m.x = 0.5;
            m.y = 128;
            m.z = 0.5;
            m.vx = m.vy = m.vz = 0;
        }
        if (m.age % 20 == 0 && rng() % 4 == 0 && !m.sitting)
            for (Session* s : tickPlayers)
                s->PlaySoundTo("mob.enderdragon.flap", m.x, m.y, m.z, 5.0f, 0.8f + static_cast<float>(rng() % 30) / 100.0f);
    }
}
