#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterTurtle(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:turtle";
        def.width = 4.0f;
        def.height = 1.333333f;
        def.health = 30.0f;
        def.speed = 0.1f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Aquatic | mobflag::Neutral;
        def.tempt = "seagrass";
        def.drops = "seagrass:0:2:1";
        def.families = "aquatic,turtle,baby_turtle,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
