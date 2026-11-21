#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPiglin(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:piglin";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 16.0f;
        def.speed = 0.42f;
        def.damage = 5.0f;
        def.flags = mobflag::Hostile | mobflag::Panic | mobflag::Ranged;
        def.tempt = "";
        def.drops = "";
        def.families = "piglin,baby_piglin,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
