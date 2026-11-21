#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterMule(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:mule";
        def.width = 1.4f;
        def.height = 1.6f;
        def.health = 30.0f;
        def.speed = 0.175f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "golden_apple,appleEnchanted,golden_carrot";
        def.drops = "leather:0:2:1";
        def.families = "mule,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
