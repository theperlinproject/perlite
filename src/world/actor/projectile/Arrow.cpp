#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::Shoot(Mob& m, Session& at) {
        Mob* arrow = Spawn("minecraft:arrow", m.x, m.y + MobRegistry::Instance().BodyOf(m).height * 0.85, m.z, false);
        if (!arrow)
            return;
        Mob* self = Find(m.id);
        const double dx = at.X() - arrow->x, dy = (at.Y() - 0.6) - arrow->y, dz = at.Z() - arrow->z;
        const double flat = std::sqrt(dx * dx + dz * dz);
        const double len = std::max(0.01, std::sqrt(dx * dx + dy * dy + dz * dz));
        const double speed = 1.6;
        arrow->vx = dx / len * speed;
        arrow->vy = (dy + flat * 0.2) / len * speed;
        arrow->vz = dz / len * speed;
        arrow->owner = self ? self->id : 0;
        arrow->yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
        for (Session* s : server.PlayersIn(kOverworld))
            s->MobMotion(*arrow);
    }

    void ActorManager::TickProjectile(Mob& m) {
        if (!(m.vx || m.vz || m.vy))
            return;
        Session* shooter = m.owner && m.owner < 5'000'000 ? server.FindSession(m.owner) : nullptr;
        const Mob* shooterMob = m.owner >= 5'000'000 ? Find(m.owner) : nullptr;
        const double speed = std::sqrt(m.vx * m.vx + m.vy * m.vy + m.vz * m.vz);
        float damage = static_cast<float>(std::ceil(speed * 2.0));
        if (m.baby)
            damage += static_cast<float>(rng() % static_cast<unsigned>(std::max(1.0f, damage / 2.0f + 2.0f)));
        for (Session* s : server.PlayersIn(kOverworld)) {
            if (s->Spectator() || s->GameMode() == 1 || s->Dead())
                continue;
            if (shooter && (s == shooter ? m.age < 5 : !server.RuleValue("pvp")))
                continue;
            const double dx = s->X() - m.x, dy = (s->Y() - 0.9) - m.y, dz = s->Z() - m.z;
            if (std::abs(dx) < 0.55 && std::abs(dz) < 0.55 && std::abs(dy) < 1.0) {
                const std::string from = shooter ? shooter->Name() : shooterMob ? std::string(shooterMob->def->id) : std::string();
                s->Damage(damage, Session::DamageCause::Projectile, m.x - m.vx, m.z - m.vz, from, shooter);
                if (shooter)
                    shooter->PlaySound("BowHit", m.x, m.y, m.z);
                m.age = 2000;
                return;
            }
        }
        for (Mob& o : mobs) {
            if (&o == &m || o.kind != actorkind::Mob || o.projectile || o.deathTime || o.id == m.owner)
                continue;
            if (!shooter && (o.def->flags & mobflag::Hostile))
                continue;
            const MobBody body = MobRegistry::Instance().BodyOf(o);
            const double hw = body.width * 0.5 + 0.15;
            const double h = body.height;
            if (std::abs(o.x - m.x) < hw && std::abs(o.z - m.z) < hw && m.y >= o.y - 0.1 && m.y <= o.y + h) {
                Hurt(o, damage, m.x - m.vx, m.z - m.vz, shooter);
                m.age = 2000;
                return;
            }
        }
    }
}
