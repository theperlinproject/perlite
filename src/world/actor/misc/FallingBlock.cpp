#include "world/actor/ActorInternal.hpp"

namespace perlite {
    bool FallsWithGravity(uint16_t id) {
        if (id == 0)
            return false;
        static std::vector<int8_t> memo;
        static std::mutex memoMutex;
        std::lock_guard lock(memoMutex);
        if (id >= memo.size())
            memo.resize(static_cast<size_t>(id) + 1, -1);
        int8_t& v = memo[id];
        if (v < 0) {
            const std::string& name = BlockRegistry::Instance().Name(id);
            v = name == "minecraft:sand" || name == "minecraft:red_sand" || name == "minecraft:gravel" ||
                        name == "minecraft:suspicious_sand" || name == "minecraft:suspicious_gravel" ||
                        name.ends_with("concrete_powder") || name.ends_with("anvil") || name == "minecraft:dragon_egg"
                    ? 1
                    : 0;
        }
        return v == 1;
    }

    bool FallingFree(uint16_t id) {
        if (id == 0 || BlockLiquid(id))
            return true;
        static std::vector<int8_t> memo;
        static std::mutex memoMutex;
        std::lock_guard lock(memoMutex);
        if (id >= memo.size())
            memo.resize(static_cast<size_t>(id) + 1, -1);
        int8_t& v = memo[id];
        if (v < 0) {
            const std::string& name = BlockRegistry::Instance().Name(id);
            v = name == "minecraft:air" || name == "minecraft:fire" || name == "minecraft:soul_fire" ||
                        name == "minecraft:short_grass" || name == "minecraft:tall_grass" || name == "minecraft:fern" ||
                        name == "minecraft:large_fern" || name == "minecraft:dead_bush" || name == "minecraft:seagrass" ||
                        name == "minecraft:tall_seagrass" || name == "minecraft:vine" || name == "minecraft:structure_void" ||
                        name == "minecraft:light_block" || name.ends_with("_air") || name == "minecraft:bush" ||
                        name == "minecraft:short_dry_grass" || name == "minecraft:tall_dry_grass"
                    ? 1
                    : 0;
        }
        return v == 1;
    }

    Mob* ActorManager::SpawnFallingBlock(double x, double y, double z, uint16_t block) {
        static const MobDef falling{"minecraft:falling_block", 0.98f, 0.98f, 1, 0, 0, 0, "", "", ""};
        Mob m;
        m.def = &falling;
        m.kind = actorkind::FallingBlock;
        m.block = block;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.health = 1;
        m.persistent = true;
        Mob& added = Add(m);
        for (Session* s : server.PlayersIn(dimension))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    bool ActorManager::TickFallingBlock(Mob& m) {
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < kMinY || by >= kMinY + kHeight || !World().Peek({bx >> 4, bz >> 4}))
                return BlockRegistry::kAir;
            return World().GetBlock(bx, by, bz);
        };
        const int bx = static_cast<int>(std::floor(m.x)), bz = static_cast<int>(std::floor(m.z));
        if (m.age > 600 || m.y < kMinY - 64)
            return true;
        m.vy = (m.vy - 0.04) * 0.98;
        const double ny = m.y + m.vy;
        for (int c = static_cast<int>(std::floor(m.y)) - (m.y == std::floor(m.y) ? 1 : 0); c >= static_cast<int>(std::floor(ny)); c--) {
            if (c < kMinY)
                return true;
            if (!FallingFree(blockAt(bx, c, bz))) {
                const int place = c + 1;
                if (FallingFree(blockAt(bx, place, bz)))
                    server.SetBlockIn(dimension, bx, place, bz, m.block);
                return true;
            }
            if (c == static_cast<int>(std::floor(ny)))
                break;
        }
        m.y = ny;
        return false;
    }
}
