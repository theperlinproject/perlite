#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterFrog(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:frog";
        def.width = 0.5f;
        def.height = 0.55f;
        def.health = 10.0f;
        def.speed = 0.1f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Aquatic;
        def.tempt = "slime_ball";
        def.drops = "";
        def.families = "frog,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
