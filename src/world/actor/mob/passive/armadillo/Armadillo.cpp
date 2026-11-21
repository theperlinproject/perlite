#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterArmadillo(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:armadillo";
        def.width = 0.7f;
        def.height = 0.65f;
        def.health = 12.0f;
        def.speed = 0.14f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "spider_eye";
        def.drops = "";
        def.families = "armadillo,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
