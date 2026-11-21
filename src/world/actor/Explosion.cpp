#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::Explosion(double x, double y, double z, float power, uint64_t source, bool breakBlocks) {
        if (server.HookExplosion(x, y, z, power, source) || power <= 0)
            return;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const Mob* origin = Find(source);
        const bool fromTnt = origin && origin->kind == actorkind::PrimedTnt;
        const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y)), cz = static_cast<int>(std::floor(z));
        const bool underwater = BlockLiquid(server.Chunks().GetBlock(cx, cy, cz));
        std::vector<std::array<int, 3>> broken;
        if (breakBlocks && !underwater) {
            std::unordered_set<int64_t> seen;
            for (int i = 0; i < 16; i++)
                for (int j = 0; j < 16; j++)
                    for (int k = 0; k < 16; k++) {
                        if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15)
                            continue;
                        double dx = i / 15.0 * 2.0 - 1.0, dy = j / 15.0 * 2.0 - 1.0, dz = k / 15.0 * 2.0 - 1.0;
                        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                        dx /= len;
                        dy /= len;
                        dz /= len;
                        float intensity = power * (0.7f + unit(rng) * 0.6f);
                        double px = x, py = y, pz = z;
                        while (intensity > 0.0f) {
                            const int bx = static_cast<int>(std::floor(px)), by = static_cast<int>(std::floor(py)),
                                      bz = static_cast<int>(std::floor(pz));
                            if (by < kMinY || by >= kMinY + kHeight || !server.Chunks().Peek({bx >> 4, bz >> 4}))
                                break;
                            const uint16_t b = server.Chunks().GetBlock(bx, by, bz);
                            if (b != 0)
                                intensity -= (BlastResistance(b) + 0.3f) * 0.3f;
                            if (intensity > 0.0f && b != 0 && !BlockLiquid(b)) {
                                const int64_t key = (static_cast<int64_t>(bx) & 0x3FFFFFF) << 38 |
                                                    (static_cast<int64_t>(bz) & 0x3FFFFFF) << 12 |
                                                    (static_cast<int64_t>(by + 2048) & 0xFFF);
                                if (seen.insert(key).second)
                                    broken.push_back({bx, by, bz});
                            }
                            px += dx * 0.3;
                            py += dy * 0.3;
                            pz += dz * 0.3;
                            intensity -= 0.22500001f;
                        }
                    }
        }
        for (const auto& b : broken) {
            const uint16_t id = server.Chunks().GetBlock(b[0], b[1], b[2]);
            if (BlockRegistry::Instance().Name(id) == "minecraft:tnt") {
                server.SetBlockIn(kOverworld, b[0], b[1], b[2], BlockRegistry::kAir);
                SpawnPrimedTnt(b[0] + 0.5, b[1], b[2] + 0.5, 10 + static_cast<int>(rng() % 20));
                continue;
            }
            if (server.RuleValue("dotiledrops")) {
                const bool decay = !fromTnt || server.RuleValue("tntexplosiondropdecay");
                server.DropBlockLoot(kOverworld, b[0], b[1], b[2], id, std::string(), decay ? power : 1.0f, nullptr);
            }
            server.SetBlockIn(kOverworld, b[0], b[1], b[2], BlockRegistry::kAir);
        }
        const double reach = power * 2.0;
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->SendLevelEvent(power >= 2.0f ? 16400 : 16390, x, y, z, 0);
            s->PlaySound("Explode", x, y, z);
            const double dx = s->X() - x, dy = s->Y() - 1.0 - y, dz = s->Z() - z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < reach && s->GameMode() != 1 && !s->Spectator()) {
                const double impact = 1.0 - d / reach;
                s->Damage(static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0),
                          Session::DamageCause::Explosion, x, z, origin && origin->kind == actorkind::Mob ? std::string(origin->def->id) : std::string(),
                          nullptr, static_cast<float>(impact));
            }
        }
        for (Mob& o : mobs) {
            if (o.id == source || o.deathTime != 0)
                continue;
            const double dx = o.x - x, dy = o.y - y, dz = o.z - z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d >= reach)
                continue;
            const double impact = 1.0 - d / reach;
            if (o.kind != actorkind::Mob) {
                const double len = std::max(0.01, d);
                o.vx += dx / len * impact;
                o.vy += dy / len * impact;
                o.vz += dz / len * impact;
                continue;
            }
            Hurt(o, static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0), x, z, nullptr);
        }
    }

    void ActorManager::Explode(Mob& m) {
        Explosion(m.x, m.y + 0.5, m.z, MobRegistry::Instance().BehaviorOf(m.def).ExplosionPower(m), m.id,
                  server.RuleValue("mobgriefing"));
        m.health = 0;
        m.deathTime = 19;
        for (Session* s : server.PlayersIn(kOverworld))
            s->HideItem(m.id);
    }
}
