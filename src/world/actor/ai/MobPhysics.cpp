#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::Physics(Mob& m) {
        const uint16_t here = server.Chunks().GetBlock(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y + 0.2)),
                                                       static_cast<int>(std::floor(m.z)));
        m.inWater = BlockLiquid(here);
        const bool flying = (m.def->flags & mobflag::Fly) != 0;
        const bool swimmer = (m.def->flags & mobflag::Aquatic) != 0;
        if (m.projectile) {
            m.vy -= 0.05;
            m.vx *= 0.99;
            m.vy *= 0.99;
            m.vz *= 0.99;
        } else if (m.inWater) {
            if (swimmer)
                m.vy *= 0.8;
            else
                m.vy = m.vy * 0.8 + 0.03;
            m.vx *= 0.8;
            m.vz *= 0.8;
        } else if (flying) {
            m.vx *= 0.91;
            m.vy *= 0.91;
            m.vz *= 0.91;
        } else {
            m.vy = (m.vy - 0.08) * 0.98;
        }
        double nx = m.x + m.vx, ny = m.y + m.vy, nz = m.z + m.vz;
        const bool wasGround = m.onGround;
        m.onGround = false;
        if (m.projectile) {
            if (Solid(nx, ny, nz)) {
                m.vx = m.vy = m.vz = 0;
                m.age = std::max(m.age, 1100);
                return;
            }
            m.x = nx;
            m.y = ny;
            m.z = nz;
            return;
        }
        if (!Collides(m, m.x, ny, m.z)) {
            m.y = ny;
        } else {
            if (m.vy < 0) {
                m.onGround = true;
                m.y = std::floor(ny) + 1.0;
                if (Collides(m, m.x, m.y, m.z))
                    m.y = std::floor(m.y + 0.5);
            }
            m.vy = 0;
        }
        bool blocked = false;
        if (!Collides(m, nx, m.y, m.z))
            m.x = nx;
        else
            blocked = true, m.vx = 0;
        if (!Collides(m, m.x, m.y, nz))
            m.z = nz;
        else
            blocked = true, m.vz = 0;
        if (blocked && (wasGround || m.onGround || m.inWater) && m.hasGoal && !flying) {
            if (!Collides(m, m.x, m.y + 1.05, m.z))
                m.vy = m.inWater ? 0.12 : 0.42;
            else
                m.hasGoal = false;
        }
        if (m.def->flags & mobflag::Spider && blocked && m.hasGoal)
            m.vy = 0.2;
        if (m.onGround) {
            m.vx *= 0.546;
            m.vz *= 0.546;
        } else if (!flying && !m.inWater) {
            m.vx *= 0.91;
            m.vz *= 0.91;
        }
        if (m.y < kMinY - 64)
            m.health = 0;
    }
}
