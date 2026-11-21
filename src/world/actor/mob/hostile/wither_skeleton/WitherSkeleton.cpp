#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWitherSkeleton(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:wither_skeleton";
        def.width = 0.72f;
        def.height = 2.01f;
        def.health = 20.0f;
        def.speed = 0.25f;
        def.damage = 4.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "coal:0:1:1;bone:0:2:1;skull:1:1:0.025";
        def.families = "wither,monster,undead,skeleton,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
