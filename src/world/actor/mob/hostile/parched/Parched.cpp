#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterParched(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:parched";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 16.0f;
        def.speed = 0.25f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "arrow:0:2:1;bone:0:2:1;arrow:0:2:1";
        def.families = "parched,skeleton,monster,mob,undead";
        registry.Add(def, MobCategory::Hostile);
    }
}
