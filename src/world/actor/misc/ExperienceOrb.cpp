#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::SpawnXpOrbs(double x, double y, double z, int amount) {
        static const MobDef orb{"minecraft:xp_orb", 0.25f, 0.25f, 1, 0, 0, 0, "", "", ""};
        static constexpr int kSizes[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1};
        int spawned = 0;
        while (amount > 0 && spawned < 24) {
            int value = 1;
            for (int s : kSizes)
                if (amount >= s) {
                    value = s;
                    break;
                }
            if (spawned == 23)
                value = amount;
            amount -= value;
            spawned++;
            Mob m;
            m.def = &orb;
            m.kind = actorkind::ExperienceOrb;
            m.block = static_cast<uint16_t>(std::min(value, 65535));
            m.id = nextId++;
            m.x = x;
            m.y = y;
            m.z = z;
            std::uniform_real_distribution<double> spread(-0.1, 0.1);
            m.vx = spread(rng) * 2.0;
            m.vy = 0.2 + std::abs(spread(rng));
            m.vz = spread(rng) * 2.0;
            m.health = 1;
            m.persistent = true;
            Add(m);
            Mob& added = mobs.back();
            for (Session* s : server.PlayersIn(kOverworld)) {
                s->ShowMob(added);
                s->MobMotion(added);
            }
            added.lastX = static_cast<float>(x);
            added.lastY = static_cast<float>(y);
            added.lastZ = static_cast<float>(z);
            added.lastFlags = Flags(added);
        }
    }

    bool ActorManager::TickExperienceOrb(Mob& m) {
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < kMinY || by >= kMinY + kHeight || !server.Chunks().Peek({bx >> 4, bz >> 4}))
                return BlockRegistry::kAir;
            return server.Chunks().GetBlock(bx, by, bz);
        };
        if (m.age > 6000 || m.y < kMinY - 64)
            return true;
        Session* best = nullptr;
        double bestD = 8.0 * 8.0;
        for (Session* s : server.PlayersIn(kOverworld)) {
            if (s->Spectator() || s->Dead())
                continue;
            const double dx = s->X() - m.x, dy = (s->Y() - 1.0) - m.y, dz = s->Z() - m.z;
            const double d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        if (best && m.age > 10) {
            const double dx = best->X() - m.x, dy = (best->Y() - 1.0) - m.y, dz = best->Z() - m.z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 1.25) {
                best->AddExperience(m.block);
                for (Session* s : server.PlayersIn(kOverworld))
                    s->TakeItem(m.id, best->Id());
                return true;
            }
            const double pull = 1.0 - d / 8.0;
            const double strength = pull * pull * 0.1;
            m.vx += dx / d * strength;
            m.vy += dy / d * strength;
            m.vz += dz / d * strength;
        }
        m.vy -= 0.03;
        const auto solid = [&](double px, double py, double pz) {
            return BlockSolid(blockAt(static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py)),
                                      static_cast<int>(std::floor(pz))));
        };
        double nx = m.x + m.vx, ny = m.y + m.vy, nz = m.z + m.vz;
        m.onGround = false;
        if (solid(m.x, ny, m.z)) {
            if (m.vy < 0) {
                ny = std::floor(ny) + 1.0;
                m.onGround = true;
            } else {
                ny = m.y;
            }
            m.vy = 0;
        }
        if (solid(nx, ny + 0.05, m.z)) {
            nx = m.x;
            m.vx = 0;
        }
        if (solid(nx, ny + 0.05, nz)) {
            nz = m.z;
            m.vz = 0;
        }
        m.x = nx;
        m.y = ny;
        m.z = nz;
        m.vx *= m.onGround ? 0.6 : 0.98;
        m.vy *= 0.98;
        m.vz *= m.onGround ? 0.6 : 0.98;
        return false;
    }
}
