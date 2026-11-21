#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterHoglin(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:hoglin";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 40.0f;
        def.speed = 0.36f;
        def.damage = 1.0f;
        def.flags = mobflag::Panic | mobflag::Neutral;
        def.tempt = "";
        def.drops = "porkchop:2:4:1;leather:0:1:1";
        def.families = "hoglin,hoglin_baby,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
