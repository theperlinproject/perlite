#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPolarBear(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:polar_bear";
        def.width = 1.4f;
        def.height = 1.4f;
        def.health = 30.0f;
        def.speed = 0.25f;
        def.damage = 6.0f;
        def.flags = mobflag::Panic | mobflag::Neutral;
        def.tempt = "";
        def.drops = "fish:0:2:1;salmon:0:2:1";
        def.families = "polarbear,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
