#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterZombiePigman(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:zombie_pigman";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 20.0f;
        def.speed = 0.23f;
        def.damage = 5.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "rotten_flesh:0:1:1;gold_nugget:0:1:1;gold_ingot:1:1:0.025";
        def.families = "baby_undead,baby_zombie_pigman,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
