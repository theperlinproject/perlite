#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCamel(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:camel";
        def.width = 1.7f;
        def.height = 2.375f;
        def.health = 32.0f;
        def.speed = 0.09f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "cactus";
        def.drops = "";
        def.families = "camel,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
