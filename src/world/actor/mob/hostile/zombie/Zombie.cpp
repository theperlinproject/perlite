#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterZombie(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:zombie";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 20.0f;
        def.speed = 0.23f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile | mobflag::Burns;
        def.tempt = "";
        def.drops = "rotten_flesh:0:2:1;iron_ingot:1:1:0.025";
        def.families = "zombie,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
