#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterMooshroom(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:mooshroom";
        def.width = 0.9f;
        def.height = 1.3f;
        def.health = 10.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "wheat";
        def.drops = "leather:0:2:1;beef:1:3:1";
        def.families = "mushroomcow,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
