#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterGhast(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:ghast";
        def.width = 4.02f;
        def.height = 4.0f;
        def.health = 10.0f;
        def.speed = 0.03f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile | mobflag::Fly | mobflag::Ranged;
        def.tempt = "";
        def.drops = "ghast_tear:0:1:1;gunpowder:0:2:1";
        def.families = "ghast,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
