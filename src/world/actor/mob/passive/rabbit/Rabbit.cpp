#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterRabbit(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:rabbit";
        def.width = 0.6f;
        def.height = 1.0f;
        def.health = 3.0f;
        def.speed = 0.3f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "golden_carrot,carrot,dandelion";
        def.drops = "rabbit_hide:0:1:1;rabbit:0:1:1;rabbit_foot:1:1:0.1";
        def.families = "rabbit,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
