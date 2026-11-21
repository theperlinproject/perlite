#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPillager(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:pillager";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 24.0f;
        def.speed = 0.35f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "arrow:0:2:1";
        def.families = "pillager,monster,illager,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
