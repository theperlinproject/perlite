#include "world/actor/ActorInternal.hpp"

namespace perlite {
    void ActorManager::SpawnPack(const std::string& category, int x, int z, int surfaceY, bool chunkGen) {
        const auto entry = World().Peek({x >> 4, z >> 4});
        if (!entry)
            return;
        const json::Value* table = World().Backend().Spawns(entry->column.GenBiome(x & 15, z & 15));
        if (!table)
            return;
        const json::Value& list = (*table)[category];
        if (!list.IsArray() || list.items.empty())
            return;
        int64_t total = 0;
        for (const auto& e : list.items)
            total += e["weight"].Int(1);
        if (total <= 0)
            return;
        int64_t pick = static_cast<int64_t>(rng() % static_cast<uint64_t>(total));
        const json::Value* chosen = nullptr;
        for (const auto& e : list.items) {
            pick -= e["weight"].Int(1);
            if (pick < 0) {
                chosen = &e;
                break;
            }
        }
        if (!chosen)
            return;
        const std::string type = (*chosen)["type"].text;
        const MobDef* def = FindMob(type.c_str());
        if (!def)
            return;
        int count = 1;
        const json::Value& c = (*chosen)["count"];
        if (c.kind == json::Value::Kind::Number)
            count = static_cast<int>(c.Int());
        else if (c.IsObject())
            count = static_cast<int>(c["min_inclusive"].Int(1) +
                                     static_cast<int64_t>(rng() % static_cast<unsigned>(std::max<int64_t>(1, c["max_inclusive"].Int(1) - c["min_inclusive"].Int(1) + 1))));
        count = std::clamp(count, 1, 8);
        const bool water = (def->flags & mobflag::Aquatic) != 0;
        std::vector<std::pair<int, int>> taken;
        for (int i = 0; i < count; i++) {
            const int px = x + static_cast<int>(rng() % 7) - 3, pz = z + static_cast<int>(rng() % 7) - 3;
            // One mob to a block: a pack spreads out instead of piling into the same spot.
            if (std::find(taken.begin(), taken.end(), std::make_pair(px, pz)) != taken.end())
                continue;
            const int y = chunkGen ? SurfaceY(px, pz) : surfaceY;
            if (y <= kMinY)
                continue;
            const uint16_t ground = World().GetBlock(px, y - 1, pz);
            if (water != BlockLiquid(ground) && !water)
                continue;
            if (water && !BlockLiquid(ground))
                continue;
            const double sy = water ? y - 2.0 : y;
            Mob probe;
            probe.def = def;
            if (Collides(probe, px + 0.5, sy, pz + 0.5))
                continue;
            if (!water && category == "creature") {
                const std::string g = BlockRegistry::Instance().Name(ground);
                if (!g.contains("grass_block") && !g.contains("snow") && !g.contains("sand") && !g.contains("podzol") &&
                    !g.contains("mycelium") && !g.contains("dirt") && !g.contains("moss"))
                    continue;
            }
            bool crowded = false;
            for (const Mob& other : mobs)
                if (!other.deathTime && std::abs(other.x - (px + 0.5)) < 1.0 && std::abs(other.z - (pz + 0.5)) < 1.0 &&
                    std::abs(other.y - sy) < 2.0) {
                    crowded = true;
                    break;
                }
            if (crowded)
                continue;
            taken.emplace_back(px, pz);
            Mob* m = Spawn(def->id, px + 0.5, sy, pz + 0.5, category == "creature");
            if (m && (def->flags & mobflag::Hostile) == 0 && rng() % 20 == 0)
                m->baby = true;
        }
    }

    void ActorManager::OnChunkReady(int chunkX, int chunkZ) {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) | static_cast<uint32_t>(chunkZ);
        if (!spawnedChunks.insert(key).second)
            return;
        if (!server.RuleValue("domobspawning"))
            return;
        if (std::uniform_real_distribution<double>(0, 1)(rng) >= 0.1)
            return;
        const int x = chunkX * 16 + static_cast<int>(rng() % 16), z = chunkZ * 16 + static_cast<int>(rng() % 16);
        SpawnPack("creature", x, z, 0, true);
    }

    void ActorManager::NaturalSpawns() {
        if (!server.RuleValue("domobspawning"))
            return;
        const std::vector<Session*> players = server.PlayersIn(dimension);
        if (players.empty())
            return;
        int monsters = 0, water = 0, ambient = 0;
        for (const Mob& m : mobs) {
            if (m.deathTime || m.projectile)
                continue;
            if (m.def->flags & mobflag::Hostile)
                monsters++;
            else if (m.def->flags & mobflag::Aquatic)
                water++;
            else if (m.def->flags & mobflag::Fly)
                ambient++;
        }
        const size_t n = players.size();
        for (Session* s : players) {
            if (s->Spectator())
                continue;
            const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
            const double r = 24 + static_cast<double>(rng() % 80);
            const int x = static_cast<int>(std::floor(s->X() + std::cos(a) * r));
            const int z = static_cast<int>(std::floor(s->Z() + std::sin(a) * r));
            if (!World().Peek({x >> 4, z >> 4}))
                continue;
            const int surface = SurfaceY(x, z);
            if (surface <= kMinY)
                continue;
            if (server.Config().difficulty > 0 && monsters < static_cast<int>(40 * n)) {
                int y = surface;
                if (rng() % 3 == 0) {
                    y = kMinY + 8 + static_cast<int>(rng() % static_cast<unsigned>(std::max(1, surface - kMinY - 8)));
                    while (y > kMinY + 1 && !BlockSolid(World().GetBlock(x, y - 1, z)))
                        y--;
                    if (BlockSolid(World().GetBlock(x, y, z)) || BlockSolid(World().GetBlock(x, y + 1, z)))
                        y = surface;
                }
                if (Dark(x, y, z) && !BlockLiquid(World().GetBlock(x, y - 1, z)))
                    SpawnPack("monster", x, z, y, false);
            }
            if (water < static_cast<int>(5 * n) && rng() % 4 == 0)
                SpawnPack("water_creature", x, z, surface, true);
            if (ambient < static_cast<int>(8 * n) && rng() % 8 == 0 && !Day())
                SpawnPack("ambient", x, z, surface, true);
        }
    }
}
