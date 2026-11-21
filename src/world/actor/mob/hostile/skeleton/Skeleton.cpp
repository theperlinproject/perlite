#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSkeleton(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:skeleton";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 20.0f;
        def.speed = 0.25f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Burns | mobflag::Ranged;
        def.tempt = "";
        def.drops = "arrow:0:2:1;bone:0:2:1";
        def.families = "skeleton,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
