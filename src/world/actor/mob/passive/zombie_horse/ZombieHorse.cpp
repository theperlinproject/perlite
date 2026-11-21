#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterZombieHorse(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:zombie_horse";
        def.width = 1.96f;
        def.height = 2.24f;
        def.health = 25.0f;
        def.speed = 0.275f;
        def.damage = 0.0f;
        def.flags = mobflag::Burns;
        def.tempt = "red_mushroom";
        def.drops = "rotten_flesh:2:3:1";
        def.families = "zombiehorse,undead,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
