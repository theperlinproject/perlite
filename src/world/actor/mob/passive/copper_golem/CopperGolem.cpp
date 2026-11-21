#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCopperGolem(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:copper_golem";
        def.width = 0.6f;
        def.height = 0.98f;
        def.health = 12.0f;
        def.speed = 0.2f;
        def.damage = 2.0f;
        def.flags = mobflag::Panic;
        def.tempt = "";
        def.drops = "copper_ingot:1:3:1";
        def.families = "copper_golem,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
