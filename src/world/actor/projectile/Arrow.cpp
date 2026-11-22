// Projectiles: arrows, snowballs, eggs, ender pearls and bottles o' enchanting. Each tick a projectile
// sweeps the segment it travels along against the blocks and the bodies of players and mobs, so a
// fast arrow cannot pass through anything; the nearest hit wins. Arrows stick in blocks and can be
// picked up; the thrown ones break where they land and do their thing.
#include "world/actor/ActorInternal.hpp"

namespace perlite {
    namespace {
        const MobDef* DefOf(int type) {
            static const MobDef arrow{"minecraft:arrow", 0.25f, 0.25f, 1, 0, 2, 0, "", "", ""};
            static const MobDef snowball{"minecraft:snowball", 0.25f, 0.25f, 1, 0, 0, 0, "", "", ""};
            static const MobDef egg{"minecraft:egg", 0.25f, 0.25f, 1, 0, 0, 0, "", "", ""};
            static const MobDef pearl{"minecraft:ender_pearl", 0.25f, 0.25f, 1, 0, 0, 0, "", "", ""};
            static const MobDef bottle{"minecraft:xp_bottle", 0.25f, 0.25f, 1, 0, 0, 0, "", "", ""};
            switch (type) {
            case projectile::Arrow:
                return &arrow;
            case projectile::Snowball:
                return &snowball;
            case projectile::Egg:
                return &egg;
            case projectile::EnderPearl:
                return &pearl;
            default:
                return &bottle;
            }
        }

        // Where the segment from (x, y, z) along (dx, dy, dz) first enters the box, as a fraction
        // of the segment, or a negative number when it misses.
        double SweepBox(double x, double y, double z, double dx, double dy, double dz, double minX, double minY,
                        double minZ, double maxX, double maxY, double maxZ) {
            double t0 = 0.0, t1 = 1.0;
            const double origin[3] = {x, y, z}, dir[3] = {dx, dy, dz};
            const double lo[3] = {minX, minY, minZ}, hi[3] = {maxX, maxY, maxZ};
            for (int a = 0; a < 3; a++) {
                if (std::abs(dir[a]) < 1e-12) {
                    if (origin[a] < lo[a] || origin[a] > hi[a])
                        return -1.0;
                    continue;
                }
                double ta = (lo[a] - origin[a]) / dir[a], tb = (hi[a] - origin[a]) / dir[a];
                if (ta > tb)
                    std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                if (t0 > t1)
                    return -1.0;
            }
            return t0;
        }
    }

    Mob* ActorManager::SpawnProjectile(int type, double x, double y, double z, double vx, double vy, double vz,
                                       uint64_t owner, bool pickup) {
        Mob m;
        m.def = DefOf(type);
        m.kind = actorkind::Projectile;
        m.projectile = true;
        m.shot = static_cast<uint8_t>(type);
        m.canPickup = pickup;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.vx = vx;
        m.vy = vy;
        m.vz = vz;
        m.owner = owner;
        m.health = 1;
        m.persistent = true;
        const double flat = std::sqrt(vx * vx + vz * vz);
        m.yaw = m.headYaw = static_cast<float>(std::atan2(-vx, vz) * 180.0 / kPi);
        m.pitch = static_cast<float>(-std::atan2(vy, flat) * 180.0 / kPi);
        mobs.push_back(m);
        Mob& added = mobs.back();
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->ShowMob(added);
            s->MobMotion(added);
        }
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastYaw = added.yaw;
        added.lastHead = added.headYaw;
        added.lastFlags = Flags(added);
        return &added;
    }

    void ActorManager::Shoot(Mob& m, Session& at) {
        const double sx = m.x, sy = m.y + MobRegistry::Instance().BodyOf(m).height * 0.85, sz = m.z;
        const double dx = at.X() - sx, dy = (at.Y() - 0.6) - sy, dz = at.Z() - sz;
        const double flat = std::sqrt(dx * dx + dz * dz);
        const double len = std::max(0.01, std::sqrt(dx * dx + dy * dy + dz * dz));
        const double speed = 1.6;
        SpawnProjectile(projectile::Arrow, sx, sy, sz, dx / len * speed, (dy + flat * 0.2) / len * speed,
                        dz / len * speed, m.id, false);
    }

    void ActorManager::TickProjectile(Mob& m) {
        (void)m; // projectiles tick through TickShot
    }

    bool ActorManager::TickShot(Mob& m) {
        ChunkManager& world = server.Chunks();
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < kMinY || by >= kMinY + kHeight || !world.Peek({bx >> 4, bz >> 4}))
                return BlockRegistry::kAir;
            return world.GetBlock(bx, by, bz);
        };
        const std::vector<Session*> players = server.PlayersIn(kOverworld);

        // An arrow in a block waits to be picked up, and falls again if the block goes.
        if (m.stuck) {
            if (!BlockSolid(blockAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)),
                                    static_cast<int>(std::floor(m.z))))) {
                m.stuck = false;
                m.vx = m.vy = m.vz = 0;
            }
            if (m.stuck) {
                if (++m.burnTicks > 1200)
                    return true;
                for (Session* s : players) {
                    if (s->Spectator() || s->Dead())
                        continue;
                    const double dx = s->X() - m.x, dy = (s->Y() - 0.9) - m.y, dz = s->Z() - m.z;
                    if (std::abs(dx) > 1.0 || std::abs(dz) > 1.0 || std::abs(dy) > 1.5)
                        continue;
                    if (!m.canPickup && !s->Creative())
                        continue;
                    if (m.canPickup && !s->Creative()) {
                        const CatalogItem* arrow = BlockCatalog::Instance().ItemByName("minecraft:arrow");
                        if (!arrow || s->Inventory().Give(arrow->networkId, 1) > 0)
                            continue;
                        s->SendInventory();
                    }
                    for (Session* o : players)
                        o->TakeItem(m.id, s->Id());
                    return true;
                }
                return false;
            }
        }

        if (m.age > 1200)
            return true;
        const double x0 = m.x, y0 = m.y, z0 = m.z;
        const double dx = m.vx, dy = m.vy, dz = m.vz;
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);

        // The first block along the segment, walked cell by cell.
        double blockT = 2.0;
        int hitFace = -1;
        if (length > 1e-9) {
            int cx = static_cast<int>(std::floor(x0)), cy = static_cast<int>(std::floor(y0)), cz = static_cast<int>(std::floor(z0));
            const int stepX = dx > 0 ? 1 : -1, stepY = dy > 0 ? 1 : -1, stepZ = dz > 0 ? 1 : -1;
            const auto boundary = [](double p, double d, int c, int step) {
                if (std::abs(d) < 1e-12)
                    return 1e30;
                const double edge = step > 0 ? c + 1.0 : static_cast<double>(c);
                return (edge - p) / d;
            };
            double tx = boundary(x0, dx, cx, stepX), ty = boundary(y0, dy, cy, stepY), tz = boundary(z0, dz, cz, stepZ);
            const double ddx = std::abs(dx) < 1e-12 ? 1e30 : std::abs(1.0 / dx);
            const double ddy = std::abs(dy) < 1e-12 ? 1e30 : std::abs(1.0 / dy);
            const double ddz = std::abs(dz) < 1e-12 ? 1e30 : std::abs(1.0 / dz);
            double t = 0.0;
            int face = -1;
            for (int guard = 0; guard < 64 && t <= 1.0; guard++) {
                const uint16_t id = blockAt(cx, cy, cz);
                if (id != BlockRegistry::kAir && BlockSolid(id)) {
                    blockT = t;
                    hitFace = face;
                    break;
                }
                if (tx < ty && tx < tz) {
                    t = tx;
                    tx += ddx;
                    cx += stepX;
                    face = stepX > 0 ? 4 : 5;
                } else if (ty < tz) {
                    t = ty;
                    ty += ddy;
                    cy += stepY;
                    face = stepY > 0 ? 0 : 1;
                } else {
                    t = tz;
                    tz += ddz;
                    cz += stepZ;
                    face = stepZ > 0 ? 2 : 3;
                }
            }
        }

        // The first body along the segment, if it comes before the block.
        Session* hitPlayer = nullptr;
        Mob* hitMob = nullptr;
        double entityT = blockT;
        const double grow = 0.3;
        for (Session* s : players) {
            if (s->Spectator() || s->Dead())
                continue;
            if (s->Id() == m.owner && m.age < 5)
                continue;
            const double feet = s->Y() - 1.62;
            const double t = SweepBox(x0, y0, z0, dx, dy, dz, s->X() - 0.3 - grow, feet - grow, s->Z() - 0.3 - grow,
                                      s->X() + 0.3 + grow, feet + 1.8 + grow, s->Z() + 0.3 + grow);
            if (t >= 0 && t < entityT) {
                entityT = t;
                hitPlayer = s;
            }
        }
        for (Mob& o : mobs) {
            if (&o == &m || o.kind != actorkind::Mob || o.projectile || o.deathTime || (o.id == m.owner && m.age < 5))
                continue;
            if (std::abs(o.x - x0) > length + 4 || std::abs(o.z - z0) > length + 4)
                continue;
            const MobBody body = MobRegistry::Instance().BodyOf(o);
            const double hw = body.width * 0.5 + grow;
            const double t = SweepBox(x0, y0, z0, dx, dy, dz, o.x - hw, o.y - grow, o.z - hw, o.x + hw,
                                      o.y + body.height + grow, o.z + hw);
            if (t >= 0 && t < entityT) {
                entityT = t;
                hitMob = &o;
                hitPlayer = nullptr;
            }
        }

        Session* shooter = m.owner && m.owner < 5'000'000 ? server.FindSession(m.owner) : nullptr;
        const Mob* shooterMob = m.owner >= 5'000'000 ? Find(m.owner) : nullptr;
        const std::string from = shooter ? shooter->Name() : shooterMob ? std::string(shooterMob->def->id) : std::string();

        if (hitPlayer || hitMob) {
            const double hx = x0 + dx * entityT, hy = y0 + dy * entityT, hz = z0 + dz * entityT;
            const double horiz = std::max(1e-6, std::sqrt(dx * dx + dz * dz));
            if (m.shot == projectile::Arrow) {
                float damage = static_cast<float>(std::ceil(std::clamp(length * 2.0, 0.0, 2147483647.0)));
                if (m.crit)
                    damage += static_cast<float>(rng() % static_cast<unsigned>(damage / 2.0f + 2.0f));
                if (hitPlayer) {
                    if (shooter && hitPlayer != shooter && !server.RuleValue("pvp")) {
                        // Arrows pass harmlessly when players cannot fight; it drops where it was.
                    } else {
                        hitPlayer->Damage(damage, Session::DamageCause::Projectile, hx - dx / horiz, hz - dz / horiz, from,
                                          shooter, 0.4f);
                    }
                } else {
                    Hurt(*hitMob, damage, hx - dx / horiz, hz - dz / horiz, shooter);
                }
                if (shooter)
                    shooter->PlaySound("BowHit", hx, hy, hz);
                return true;
            }
            // Thrown things knock back, and snowballs hurt blazes.
            const double push = 0.4;
            if (hitPlayer) {
                if (hitPlayer->GameMode() != 1)
                    hitPlayer->SetVelocity(dx / horiz * push, 0.3, dz / horiz * push);
            } else {
                const float hurt = m.shot == projectile::Snowball && std::string_view(hitMob->def->id) == "minecraft:blaze" ? 3.0f : 0.0f;
                if (hurt > 0)
                    Hurt(*hitMob, hurt, hx - dx / horiz, hz - dz / horiz, shooter);
                hitMob->vx += dx / horiz * push;
                hitMob->vz += dz / horiz * push;
                hitMob->vy = std::max(hitMob->vy, 0.3);
            }
            Land(m, hx, hy, hz, shooter);
            return true;
        }

        if (blockT <= 1.0) {
            const double hx = x0 + dx * blockT, hy = y0 + dy * blockT, hz = z0 + dz * blockT;
            if (m.shot == projectile::Arrow) {
                // Stick a little way into the block, pointing the way it flew.
                m.x = hx + dx / std::max(length, 1e-6) * 0.05;
                m.y = hy + dy / std::max(length, 1e-6) * 0.05;
                m.z = hz + dz / std::max(length, 1e-6) * 0.05;
                m.goalX = dx / std::max(length, 1e-6);
                m.goalY = dy / std::max(length, 1e-6);
                m.goalZ = dz / std::max(length, 1e-6);
                m.vx = m.vy = m.vz = 0;
                m.stuck = true;
                m.burnTicks = 0;
                (void)hitFace;
                for (Session* s : players) {
                    s->MoveMob(m);
                    s->MobMotion(m);
                    s->PlaySound("BowHit", hx, hy, hz);
                }
                m.lastX = static_cast<float>(m.x);
                m.lastY = static_cast<float>(m.y);
                m.lastZ = static_cast<float>(m.z);
                return false;
            }
            Land(m, hx, hy, hz, shooter);
            return true;
        }

        // Nothing in the way: fly on, slowed by the air or water, pulled down by gravity.
        m.x = x0 + dx;
        m.y = y0 + dy;
        m.z = z0 + dz;
        const bool water = BlockLiquid(blockAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)),
                                               static_cast<int>(std::floor(m.z))));
        const double drag = water ? (m.shot == projectile::Arrow ? 0.6 : 0.8) : 0.99;
        const double gravity = m.shot == projectile::Arrow ? 0.05 : m.shot == projectile::XpBottle ? 0.07 : 0.03;
        m.vx *= drag;
        m.vy *= drag;
        m.vz *= drag;
        m.vy -= gravity;
        const double flat = std::sqrt(m.vx * m.vx + m.vz * m.vz);
        m.yaw = m.headYaw = static_cast<float>(std::atan2(-m.vx, m.vz) * 180.0 / kPi);
        m.pitch = static_cast<float>(-std::atan2(m.vy, flat) * 180.0 / kPi);
        if (m.y < kMinY - 64)
            return true;
        return false;
    }

    // A thrown projectile breaks where it lands.
    void ActorManager::Land(Mob& m, double x, double y, double z, Session* shooter) {
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        switch (m.shot) {
        case projectile::Egg:
            if (rng() % 8 == 0) {
                const int chicks = rng() % 32 == 0 ? 4 : 1;
                for (int i = 0; i < chicks; i++)
                    if (Mob* chick = Spawn("minecraft:chicken", x, y, z, true))
                        chick->baby = true;
            }
            break;
        case projectile::EnderPearl:
            if (shooter && !shooter->Dead() && shooter->DimensionId() == kOverworld) {
                shooter->Teleport(x, y + 1.62, z);
                if (!shooter->Creative())
                    shooter->Damage(5.0f, Session::DamageCause::Fall, x, z);
                for (Session* s : players)
                    s->PlaySound("Teleport", x, y, z);
            }
            break;
        case projectile::XpBottle:
            SpawnXpOrbs(x, y, z, 3 + static_cast<int>(rng() % 5) + static_cast<int>(rng() % 5));
            for (Session* s : players)
                s->PlaySound("Glass", x, y, z);
            break;
        default:
            break;
        }
    }
}
