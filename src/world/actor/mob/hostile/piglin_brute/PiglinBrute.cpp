#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPiglinBrute(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:piglin_brute";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 50.0f;
        def.speed = 0.35f;
        def.damage = 7.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "";
        def.families = "piglin,adult_piglin,piglin_brute,monster";
        registry.Add(def, MobCategory::Hostile);
    }
}
