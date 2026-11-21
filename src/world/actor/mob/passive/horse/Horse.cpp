#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterHorse(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:horse";
        def.width = 1.4f;
        def.height = 1.6f;
        def.health = 30.0f;
        def.speed = 0.3375f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "golden_apple,appleEnchanted,golden_carrot";
        def.drops = "leather:0:2:1";
        def.families = "horse,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
