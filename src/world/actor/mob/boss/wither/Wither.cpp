#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWither(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:wither";
        def.width = 1.0f;
        def.height = 3.0f;
        def.health = 600.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile | mobflag::Fly;
        def.tempt = "";
        def.drops = "netherStar:1:1:1";
        def.families = "wither,skeleton,monster,undead,mob";
        registry.Add(def, MobCategory::Boss);
    }
}
