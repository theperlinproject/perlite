#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCamelHusk(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:camel_husk";
        def.width = 1.7f;
        def.height = 2.375f;
        def.health = 32.0f;
        def.speed = 0.09f;
        def.damage = 0.0f;
        def.flags = 0u;
        def.tempt = "rabbit_foot";
        def.drops = "rotten_flesh:2:3:1";
        def.families = "camel_husk,mob,undead";
        registry.Add(def, MobCategory::Passive);
    }
}
