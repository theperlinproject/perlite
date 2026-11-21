#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWitch(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:witch";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 26.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "glowstone_dust:0:2:1;redstone:4:8:1";
        def.families = "witch,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
