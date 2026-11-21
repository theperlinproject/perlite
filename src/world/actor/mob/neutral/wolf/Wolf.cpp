#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWolf(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:wolf";
        def.width = 0.6f;
        def.height = 0.8f;
        def.health = 8.0f;
        def.speed = 0.3f;
        def.damage = 3.0f;
        def.flags = mobflag::Panic | mobflag::Neutral;
        def.tempt = "";
        def.drops = "";
        def.families = "wolf,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
