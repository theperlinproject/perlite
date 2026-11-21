#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSkeletonHorse(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:skeleton_horse";
        def.width = 1.96f;
        def.height = 2.24f;
        def.health = 15.0f;
        def.speed = 0.2f;
        def.damage = 0.0f;
        def.flags = 0u;
        def.tempt = "";
        def.drops = "bone:0:2:1";
        def.families = "skeletonhorse,undead,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
