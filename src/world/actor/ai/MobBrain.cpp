#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::Think(Mob& m) {
        if (m.projectile) {
            TickProjectile(m);
            return;
        }
        const MobDef& d = *m.def;
        const bool hostile = (d.flags & mobflag::Hostile) != 0;
        const bool flying = (d.flags & mobflag::Fly) != 0;
        const bool swimmer = (d.flags & mobflag::Aquatic) != 0;
        double speed = d.speed * (m.baby ? 1.5 : 1.0);
        Session* target = nullptr;
        if (m.target) {
            target = server.FindSession(m.target);
            if (!target || !target->InGame() || target->Dead() || target->GameMode() == 1 || target->Spectator()) {
                m.target = 0;
                m.angry = false;
                target = nullptr;
            }
        }
        if (hostile && !target && server.Config().difficulty > 0 && m.age % 10 == 0)
            if (Session* s = Nearest(m, d.flags & mobflag::Ranged ? 16.0 : 24.0, true)) {
                if (MobRegistry::Instance().BehaviorOf(m.def).AutoTargets(m))
                    m.target = s->Id(), target = s;
            }
        if (server.Config().difficulty == 0 && hostile && !m.persistent) {
            m.health = 0;
            return;
        }
        if (d.flags & mobflag::Burns) {
            const bool sky = SurfaceY(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.z))) <= m.y + d.height;
            m.burning = Day() && sky && !m.inWater && !server.Raining() && !m.baby;
            if (m.burning && ++m.burnTicks % 20 == 0)
                Hurt(m, 1.0f, m.x, m.z, nullptr);
        }
        if (m.panicTicks > 0) {
            m.panicTicks--;
            if (!m.hasGoal || m.goalTicks <= 0) {
                const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
                m.goalX = m.x + std::cos(a) * 8;
                m.goalZ = m.z + std::sin(a) * 8;
                m.goalY = m.y;
                m.hasGoal = true;
                m.goalTicks = 40;
            }
            speed *= 1.6;
        } else if (target) {
            const double dx = target->X() - m.x, dz = target->Z() - m.z, dy = (target->Y() - 1.62) - m.y;
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (d.flags & mobflag::Explodes) {
                if (dist < 3.0 && std::abs(dy) < 3) {
                    if (m.fuse < 0)
                        m.fuse = 0;
                    m.hasGoal = false;
                } else if (m.fuse >= 0 && dist > 7.0) {
                    m.fuse = -1;
                }
            }
            if (d.flags & mobflag::Ranged) {
                m.hasGoal = dist > 10.0;
                m.goalX = target->X();
                m.goalZ = target->Z();
                m.goalY = target->Y();
                if (m.shootCooldown > 0)
                    m.shootCooldown--;
                else if (dist <= 15.0) {
                    Shoot(m, *target);
                    // The vanilla definition: every 3 s, every 2 s on hard.
                    m.shootCooldown = server.Config().difficulty >= 3 ? 40 : 60;
                }
            } else if (m.fuse < 0) {
                m.goalX = target->X();
                m.goalZ = target->Z();
                m.goalY = target->Y();
                m.hasGoal = true;
                m.goalTicks = 20;
                const MobBody body = MobRegistry::Instance().BodyOf(m);
                const double reach = body.width * 2.0 * 0.5 + 0.9;
                const float baseDamage = m.damage >= 0 ? m.damage : d.damage;
                if (dist < reach + 0.5 && std::abs(dy) < 2.0 && m.attackCooldown <= 0 && baseDamage > 0) {
                    float damage = baseDamage;
                    if (server.Config().difficulty == 1)
                        damage = std::min(damage / 2.0f + 1.0f, damage);
                    else if (server.Config().difficulty == 3)
                        damage *= 1.5f;
                    target->Damage(damage, Session::DamageCause::Mob, m.x, m.z, d.id);
                    m.attackCooldown = 20;
                    for (Session* s : server.PlayersIn(kOverworld))
                        s->MobEvent(m.id, 4, 0);
                }
            }
            speed *= hostile ? 1.0 : 1.4;
            m.headYaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
            m.pitch = static_cast<float>(-std::atan2(dy + 1.62 - d.height * 0.85, std::max(0.01, dist)) * 180.0 / kPi);
        } else {
            Session* tempter = nullptr;
            if (*d.tempt && m.age % 5 == 0)
                if (Session* s = Nearest(m, 10.0, false)) {
                    const ItemStack& held = s->Inventory().main[static_cast<size_t>(s->Inventory().selected)];
                    if (!held.Empty())
                        if (const CatalogItem* item = BlockCatalog::Instance().ItemById(held.networkId)) {
                            const std::string n = item->name.substr(item->name.find(':') + 1);
                            std::istringstream list(d.tempt);
                            std::string t;
                            while (std::getline(list, t, ','))
                                if (t == n)
                                    tempter = s;
                        }
                }
            if (tempter) {
                m.goalX = tempter->X();
                m.goalZ = tempter->Z();
                m.goalY = tempter->Y();
                const double dx = m.goalX - m.x, dz = m.goalZ - m.z;
                m.hasGoal = dx * dx + dz * dz > 4.0;
                m.goalTicks = 20;
                m.headYaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
            } else if (!m.hasGoal || m.goalTicks <= 0) {
                m.hasGoal = false;
                if (rng() % (flying || swimmer ? 40 : 120) == 0) {
                    const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
                    const double r = 3 + rng() % 8;
                    m.goalX = m.x + std::cos(a) * r;
                    m.goalZ = m.z + std::sin(a) * r;
                    m.goalY = m.y + (flying || swimmer ? static_cast<double>(static_cast<int>(rng() % 7) - 3) : 0);
                    m.hasGoal = true;
                    m.goalTicks = 120;
                } else if (m.age % 40 == 0) {
                    if (Session* s = Nearest(m, 8.0, false)) {
                        if (rng() % 3 == 0)
                            m.headYaw = static_cast<float>(std::atan2(-(s->X() - m.x), s->Z() - m.z) * 180.0 / kPi);
                    }
                }
            }
        }
        if (m.fuse >= 0) {
            if (++m.fuse >= 30) {
                Explode(m);
                return;
            }
        }
        if (m.attackCooldown > 0)
            m.attackCooldown--;
        if (m.goalTicks > 0)
            m.goalTicks--;
        if (swimmer && !m.inWater) {
            if (m.onGround && m.age % 10 == 0)
                m.vy = 0.3, m.vx += (rng() % 100 - 50) / 500.0, m.vz += (rng() % 100 - 50) / 500.0;
            if (m.age % 20 == 0)
                Hurt(m, 1.0f, m.x, m.z, nullptr);
            return;
        }
        if (m.hasGoal) {
            const double dx = m.goalX - m.x, dz = m.goalZ - m.z;
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 0.5 && !target) {
                m.hasGoal = false;
            } else {
                const double step = speed * (m.onGround || flying || m.inWater ? 0.45 : 0.08);
                m.vx += dx / std::max(dist, 0.01) * step * 0.35;
                m.vz += dz / std::max(dist, 0.01) * step * 0.35;
                const double hv = std::sqrt(m.vx * m.vx + m.vz * m.vz);
                if (hv > step && hv > 0) {
                    m.vx *= step / hv;
                    m.vz *= step / hv;
                }
                if (flying || (swimmer && m.inWater))
                    m.vy += std::clamp((m.goalY - m.y) * 0.02, -0.05, 0.05);
                const float want = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
                float diff = std::fmod(want - m.yaw + 540.0f, 360.0f) - 180.0f;
                m.yaw += std::clamp(diff, -30.0f, 30.0f);
                if (!target)
                    m.headYaw = m.yaw;
                if (!flying && !m.inWater && m.onGround && !target) {
                    const int ax = static_cast<int>(std::floor(m.x + dx / std::max(dist, 0.01)));
                    const int az = static_cast<int>(std::floor(m.z + dz / std::max(dist, 0.01)));
                    const int by = static_cast<int>(std::floor(m.y));
                    if (!Solid(ax + 0.5, by - 0.5, az + 0.5) && !Solid(ax + 0.5, by - 1.5, az + 0.5) &&
                        !Solid(ax + 0.5, by - 2.5, az + 0.5))
                        m.hasGoal = false, m.vx = 0, m.vz = 0;
                    const uint16_t ahead = server.Chunks().GetBlock(ax, by, az);
                    const uint16_t below = server.Chunks().GetBlock(ax, by - 1, az);
                    if (BlockRegistry::Instance().Name(ahead).contains("lava") ||
                        BlockRegistry::Instance().Name(below).contains("lava"))
                        m.hasGoal = false, m.vx = 0, m.vz = 0;
                }
            }
        }
    }
}
