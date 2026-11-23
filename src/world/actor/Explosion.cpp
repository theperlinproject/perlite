#include "world/actor/ActorInternal.hpp"
#include "common/ParallelFor.hpp"

#include <algorithm>
#include <climits>

namespace perlite {
    namespace {
        // The 1352 ray directions of a vanilla explosion (the surface of a 16x16x16 grid), unit length.
        std::vector<std::array<float, 3>> MakeRays(int grid) {
            std::vector<std::array<float, 3>> out;
            const double last = grid - 1;
            for (int i = 0; i < grid; i++)
                for (int j = 0; j < grid; j++)
                    for (int k = 0; k < grid; k++) {
                        if (i != 0 && i != grid - 1 && j != 0 && j != grid - 1 && k != 0 && k != grid - 1)
                            continue;
                        double dx = i / last * 2.0 - 1.0, dy = j / last * 2.0 - 1.0, dz = k / last * 2.0 - 1.0;
                        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                        out.push_back({static_cast<float>(dx / len * 0.3), static_cast<float>(dy / len * 0.3),
                                       static_cast<float>(dz / len * 0.3)});
                    }
            return out;
        }

        // The 1352 ray directions of a vanilla explosion (the surface of a 16x16x16 grid), 0.3 long.
        const std::vector<std::array<float, 3>>& Rays() {
            static const std::vector<std::array<float, 3>> rays = MakeRays(16);
            return rays;
        }

        // The rays of a 9x9x9 grid surface (386): used only when one tick has more explosions than
        // kCoarseBlasts, where hundreds of overlapping blasts make the crater anyway.
        const std::vector<std::array<float, 3>>& CoarseRays() {
            static const std::vector<std::array<float, 3>> rays = MakeRays(9);
            return rays;
        }
        constexpr size_t kCoarseBlasts = 512;

        bool IsTnt(uint16_t id) {
            static std::atomic<int8_t> cache[0x10000] = {};
            int8_t v = cache[id].load(std::memory_order_relaxed);
            if (v == 0) {
                v = BlockRegistry::Instance().Name(id) == "minecraft:tnt" ? 1 : 2;
                cache[id].store(v, std::memory_order_relaxed);
            }
            return v == 1;
        }
    }

    void ActorManager::BrickTable::Reset(const std::vector<Blast>& blasts) {
        Clear();
        bool any = false;
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0, z0 = 0, z1 = 0;
        for (const Blast& b : blasts) {
            if (!b.breakBlocks)
                continue;
            const int r = static_cast<int>(std::ceil(b.power * 1.3f / 0.225f * 0.3f)) + 1;
            const int bx = static_cast<int>(std::floor(b.x)), by = static_cast<int>(std::floor(b.y)), bz = static_cast<int>(std::floor(b.z));
            if (!any) {
                x0 = bx - r, x1 = bx + r, y0 = by - r, y1 = by + r, z0 = bz - r, z1 = bz + r;
                any = true;
            } else {
                x0 = std::min(x0, bx - r), x1 = std::max(x1, bx + r);
                y0 = std::min(y0, by - r), y1 = std::max(y1, by + r);
                z0 = std::min(z0, bz - r), z1 = std::max(z1, bz + r);
            }
        }
        if (!any)
            return;
        y0 = std::max(y0, kMinY);
        y1 = std::min(y1, kMinY + kHeight - 1);
        ox = x0 >> 2, oy = y0 >> 2, oz = z0 >> 2;
        sx = (x1 >> 2) - ox + 1, sy = (y1 >> 2) - oy + 1, sz = (z1 >> 2) - oz + 1;
        const size_t cells = static_cast<size_t>(sx) * static_cast<size_t>(sy) * static_cast<size_t>(sz);
        if (cells > (8u << 20)) { // blasts spread too far apart: no shared table this tick
            sx = sy = sz = 0;
            return;
        }
        if (cells > capacity) {
            states = std::make_unique<std::atomic<int8_t>[]>(cells);
            capacity = cells;
        }
        for (size_t i = 0; i < cells; i++)
            states[i].store(0, std::memory_order_relaxed);
    }

    // The rays of one explosion: which blocks it breaks. Reads the world only, so the explosions
    // of a tick can work this out side by side.
    void ActorManager::TraceBlast(const Blast& blast, std::vector<std::array<int, 3>>& broken) const {
        broken.clear();
        if (!blast.breakBlocks)
            return;
        const ChunkManager& world = World();
        const double x = blast.x, y = blast.y, z = blast.z;
        const float power = blast.power;
        const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y)), cz = static_cast<int>(std::floor(z));
        const int reachBlocks = static_cast<int>(std::ceil(power * 1.3f / 0.225f * 0.3f)) + 1;
        const int chunkSpan = ((reachBlocks + 15) >> 4) * 2 + 1;
        const int baseCx = (cx >> 4) - chunkSpan / 2, baseCz = (cz >> 4) - chunkSpan / 2;
        std::vector<const ChunkColumn*> columns(static_cast<size_t>(chunkSpan * chunkSpan), nullptr);
        std::vector<uint8_t> looked(columns.size(), 0);
        const auto columnAt = [&](int bx, int bz) -> const ChunkColumn* {
            const int ix = (bx >> 4) - baseCx, iz = (bz >> 4) - baseCz;
            if (ix < 0 || iz < 0 || ix >= chunkSpan || iz >= chunkSpan)
                return world.Column(bx >> 4, bz >> 4);
            const size_t slot = static_cast<size_t>(iz * chunkSpan + ix);
            if (!looked[slot]) {
                looked[slot] = 1;
                columns[slot] = world.Column(bx >> 4, bz >> 4);
            }
            return columns[slot];
        };
        const ChunkColumn* here = columnAt(cx, cz);
        if (!here || BlockLiquid(here->Get(cx & 15, cy, cz & 15)))
            return; // under water the blast breaks nothing
        std::mt19937 local(blast.seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const int side = reachBlocks * 2 + 1;
        std::vector<uint8_t> seen(static_cast<size_t>(side * side * side), 0);
        // Positions are kept relative to the blast's block (small floats floor fast), and a ray
        // remembers the block of its last step: it takes about three 0.3-block steps per block.
        const float ox = static_cast<float>(x - cx), oy = static_cast<float>(y - cy), oz = static_cast<float>(z - cz);
        const auto fastFloor = [](float v) {
            const int i = static_cast<int>(v);
            return v < static_cast<float>(i) ? i - 1 : i;
        };
        const auto blockAt = [&](int bx, int by, int bz, bool& outside) -> uint16_t {
            outside = false;
            if (by < kMinY || by >= kMinY + kHeight) {
                outside = true;
                return 0;
            }
            const ChunkColumn* column = columnAt(bx, bz);
            if (!column) {
                outside = true;
                return 0;
            }
            const ChunkColumn::Section& sec = column->SectionAt((by - kMinY) >> 4);
            return sec.cells ? (*sec.cells)[static_cast<size_t>(CellIndex(bx & 15, (by - kMinY) & 15, bz & 15))] : sec.uniform;
        };
        // 4x4x4 bricks (in world coordinates) that hold nothing but air: a ray crosses such a brick
        // in one go instead of step by step (late in a chain reaction most of what the rays cross is
        // the air earlier blasts left). The bricks of all of a tick's blasts are worked out once.
        constexpr int kBrick = 4;
        const auto emptyBrick = [&](int bx, int by, int bz) -> bool { // world block coords
            std::atomic<int8_t>* state = bricks.At(bx >> 2, by >> 2, bz >> 2);
            if (!state)
                return false;
            int8_t v = state->load(std::memory_order_relaxed);
            if (v == 0) {
                v = 1;
                const int x0 = bx & ~3, y0 = by & ~3, z0 = bz & ~3;
                for (int dy = 0; dy < 4 && v == 1; dy++)
                    for (int dz = 0; dz < 4 && v == 1; dz++)
                        for (int dx = 0; dx < 4 && v == 1; dx++) {
                            bool outside = false;
                            if (blockAt(x0 + dx, y0 + dy, z0 + dz, outside) != 0 || outside)
                                v = 2;
                        }
                state->store(v, std::memory_order_relaxed);
            }
            return v == 1;
        };
        for (const auto& ray : blast.coarse ? CoarseRays() : Rays()) {
            float intensity = power * (0.7f + unit(local) * 0.6f);
            float px = ox, py = oy, pz = oz;
            int lastX = INT32_MIN, lastY = 0, lastZ = 0;
            uint16_t b = 0;
            float cost = 0.0f;
            bool stop = false;
            while (intensity > 0.0f) {
                const int rx = fastFloor(px), ry = fastFloor(py), rz = fastFloor(pz);
                if (rx != lastX || ry != lastY || rz != lastZ) {
                    lastX = rx;
                    lastY = ry;
                    lastZ = rz;
                    if (emptyBrick(cx + rx, cy + ry, cz + rz)) {
                        // Steps that stay inside this brick: all air, only the distance costs.
                        const int ix = ((cx + rx) & ~(kBrick - 1)) - cx, iy = ((cy + ry) & ~(kBrick - 1)) - cy,
                                      iz = ((cz + rz) & ~(kBrick - 1)) - cz;
                        int steps = 1 << 20;
                        const auto inside = [&](float p, float d, int lo) {
                            if (d > 0.0f)
                                return static_cast<int>((static_cast<float>(lo + kBrick) - 1e-4f - p) / d);
                            if (d < 0.0f)
                                return static_cast<int>((static_cast<float>(lo) - p) / d);
                            return 1 << 20;
                        };
                        steps = std::min({steps, inside(px, ray[0], ix), inside(py, ray[1], iy), inside(pz, ray[2], iz)});
                        if (steps > 0) {
                            const int afford = static_cast<int>(intensity / 0.22500001f);
                            if (afford < steps)
                                break; // the ray dies in this brick of air
                            px += ray[0] * static_cast<float>(steps);
                            py += ray[1] * static_cast<float>(steps);
                            pz += ray[2] * static_cast<float>(steps);
                            intensity -= 0.22500001f * static_cast<float>(steps);
                            lastX = INT32_MIN;
                            continue;
                        }
                    }
                    bool outside = false;
                    b = blockAt(cx + rx, cy + ry, cz + rz, outside);
                    stop = outside;
                    cost = b != 0 ? (BlastResistance(b) + 0.3f) * 0.3f : 0.0f;
                }
                if (stop)
                    break;
                if (b != 0) {
                    intensity -= cost;
                    if (intensity > 0.0f && !BlockLiquid(b)) {
                        const int lx = rx + reachBlocks, ly = ry + reachBlocks, lz = rz + reachBlocks;
                        if (lx >= 0 && ly >= 0 && lz >= 0 && lx < side && ly < side && lz < side) {
                            uint8_t& bit = seen[static_cast<size_t>((ly * side + lz) * side + lx)];
                            if (!bit) {
                                bit = 1;
                                broken.push_back({cx + rx, cy + ry, cz + rz});
                            }
                        }
                    }
                }
                px += ray[0];
                py += ray[1];
                pz += ray[2];
                intensity -= 0.22500001f;
            }
        }
    }

    // What an explosion does once its blocks are known: breaks them (TNT among them is lit), and
    // hurts and pushes what is near.
    void ActorManager::ApplyBlast(const Blast& blast, const std::vector<std::array<int, 3>>& broken) {
        const double x = blast.x, y = blast.y, z = blast.z;
        const float power = blast.power;
        ChunkManager& world = World();
        if (!broken.empty()) {
            const bool drops = server.RuleValue("dotiledrops") != 0;
            const bool decay = !blast.fromTnt || server.RuleValue("tntexplosiondropdecay");
            for (const auto& b : broken) {
                const ChunkColumn* column = world.Column(b[0] >> 4, b[2] >> 4);
                if (!column)
                    continue;
                const uint16_t id = column->Get(b[0] & 15, b[1], b[2] & 15);
                if (id == BlockRegistry::kAir)
                    continue; // an earlier explosion this tick took it
                if (IsTnt(id)) {
                    server.SetBlockIn(dimension, b[0], b[1], b[2], BlockRegistry::kAir);
                    SpawnPrimedTnt(b[0] + 0.5, b[1], b[2] + 0.5, 10 + static_cast<int>(rng() % 20));
                    continue;
                }
                if (drops)
                    server.DropBlockLoot(dimension, b[0], b[1], b[2], id, std::string(), decay ? power : 1.0f, nullptr);
                server.SetBlockIn(dimension, b[0], b[1], b[2], BlockRegistry::kAir);
            }
        }
        const double reach = power * 2.0;
        // The flash and the bang: a few per tick are plenty, a thousand at once say no more.
        const bool effects = effectsThisTick++ < 24;
        for (Session* s : server.PlayersIn(dimension)) {
            const double dx = s->X() - x, dy = s->Y() - 1.0 - y, dz = s->Z() - z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (effects && d2 < 256.0 * 256.0) {
                s->SendLevelEvent(power >= 2.0f ? 16400 : 16390, x, y, z, 0);
                s->PlaySound("Explode", x, y, z);
            }
            const double d = std::sqrt(d2);
            if (d < reach && s->GameMode() != 1 && !s->Spectator()) {
                const double impact = 1.0 - d / reach;
                s->Damage(static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0),
                          Session::DamageCause::Explosion, x, z, blast.originType, nullptr, static_cast<float>(impact));
            }
        }
        ForNear(x, z, reach, [&](Mob& o) {
            if (o.id == blast.source || o.deathTime != 0)
                return;
            const double dx = o.x - x, dy = o.y - y, dz = o.z - z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d >= reach)
                return;
            const double impact = 1.0 - d / reach;
            if (o.kind == actorkind::Crystal) {
                DestroyCrystal(o, nullptr);
                return;
            }
            if (o.kind != actorkind::Mob) {
                const double len = std::max(0.01, d);
                o.vx += dx / len * impact;
                o.vy += dy / len * impact;
                o.vz += dz / len * impact;
                o.kicked = true;
                return;
            }
            Hurt(o, static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0), x, z, nullptr);
        });
    }

    ActorManager::Blast ActorManager::MakeBlast(double x, double y, double z, float power, uint64_t source, bool breakBlocks) {
        Blast blast;
        blast.x = x;
        blast.y = y;
        blast.z = z;
        blast.power = power;
        blast.source = source;
        blast.breakBlocks = breakBlocks;
        const Mob* origin = Find(source);
        blast.fromTnt = origin && origin->kind == actorkind::PrimedTnt;
        if (origin && origin->kind == actorkind::Mob)
            blast.originType = origin->def->id;
        blast.seed = static_cast<uint32_t>(rng());
        return blast;
    }

    void ActorManager::Explosion(double x, double y, double z, float power, uint64_t source, bool breakBlocks) {
        if (server.HookExplosion(x, y, z, power, source) || power <= 0)
            return;
        const Blast blast = MakeBlast(x, y, z, power, source, breakBlocks);
        std::vector<std::array<int, 3>> broken;
        TraceBlast(blast, broken);
        ApplyBlast(blast, broken);
    }

    // The TNT that went off this tick: the rays of all of them are traced in parallel against the
    // world as it was, then the explosions happen one after another in the order they were lit.
    void ActorManager::RunBlasts() {
        if (pendingBlasts.empty())
            return;
        std::vector<Blast> blasts;
        blasts.swap(pendingBlasts);
        std::vector<std::vector<std::array<int, 3>>> broken(blasts.size());
        // The 4x4x4 bricks every blast of this tick may cross: one shared table over the box around
        // all of them, each brick checked for air by whichever blast reaches it first.
        bricks.Reset(blasts);
        if (blasts.size() > kCoarseBlasts)
            for (Blast& b : blasts)
                b.coarse = true;
        ParallelPool::Instance().For(blasts.size(), [&](size_t i) { TraceBlast(blasts[i], broken[i]); });
        bricks.Clear();
        for (size_t i = 0; i < blasts.size(); i++)
            ApplyBlast(blasts[i], broken[i]);
    }

    void ActorManager::Explode(Mob& m) {
        Explosion(m.x, m.y + 0.5, m.z, MobRegistry::Instance().BehaviorOf(m.def).ExplosionPower(m), m.id,
                  server.RuleValue("mobgriefing"));
        m.health = 0;
        m.deathTime = 19;
        for (Session* s : server.PlayersIn(dimension))
            s->HideItem(m.id);
    }
}
