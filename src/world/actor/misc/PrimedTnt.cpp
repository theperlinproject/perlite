#include "world/actor/ActorInternal.hpp"

namespace perlite {
    Mob* ActorManager::SpawnPrimedTnt(double x, double y, double z, int fuse) {
        static const MobDef tnt{"minecraft:tnt", 0.98f, 0.98f, 1, 0, 0, 0, "", "", ""};
        Mob m;
        m.def = &tnt;
        m.kind = actorkind::PrimedTnt;
        m.fuse = fuse;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        const double angle = unit(rng) * 2.0 * kPi;
        m.vx = -std::sin(angle) * 0.02;
        m.vy = 0.2;
        m.vz = -std::cos(angle) * 0.02;
        m.health = 1;
        m.persistent = true;
        Mob& added = Add(m);
        const bool sound = fuseSoundsThisTick++ < 16;
        const Session::Encoded motion = Session::EncodeMobMotion(added, server.TickCount());
        for (Session* s : server.PlayersIn(dimension)) {
            s->ShowMob(added);
            s->SendEncoded(motion);
            if (sound)
                s->PlaySound("Fuse", x, y, z);
        }
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    bool ActorManager::TickPrimedTnt(Mob& m) {
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < kMinY || by >= kMinY + kHeight || !World().Peek({bx >> 4, bz >> 4}))
                return BlockRegistry::kAir;
            return World().GetBlock(bx, by, bz);
        };
        m.vy -= 0.04;
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
        if (solid(nx, ny + 0.1, m.z)) {
            nx = m.x;
            m.vx = 0;
        }
        if (solid(nx, ny + 0.1, nz)) {
            nz = m.z;
            m.vz = 0;
        }
        m.x = nx;
        m.y = ny;
        m.z = nz;
        m.vx *= 0.98;
        m.vy *= 0.98;
        m.vz *= 0.98;
        if (m.onGround) {
            m.vx *= 0.7;
            m.vz *= 0.7;
        }
        if (--m.fuse <= 0) {
            // Goes off with the rest of this tick's TNT (see RunBlasts).
            float power = 4.0f;
            if (!server.HookExplosion(m.x, m.y + 0.0625, m.z, power, m.id) && power > 0)
                pendingBlasts.push_back(MakeBlast(m.x, m.y + 0.0625, m.z, power, m.id, server.RuleValue("tntexplodes") != 0));
            return true;
        }
        return false;
    }
}
