#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCat(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:cat";
        def.width = 0.6f;
        def.height = 0.7f;
        def.health = 10.0f;
        def.speed = 0.3f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "fish,salmon";
        def.drops = "string:0:2:1";
        def.families = "cat,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
