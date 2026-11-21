#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterGoat(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:goat";
        def.width = 0.9f;
        def.height = 1.3f;
        def.health = 10.0f;
        def.speed = 0.4f;
        def.damage = 1.0f;
        def.flags = mobflag::Panic;
        def.tempt = "wheat";
        def.drops = "";
        def.families = "goat,animal";
        registry.Add(def, MobCategory::Passive);
    }
}
