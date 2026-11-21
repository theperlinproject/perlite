#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterZombieNautilus(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:zombie_nautilus";
        def.width = 0.875f;
        def.height = 0.95f;
        def.health = 15.0f;
        def.speed = 0.15f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile | mobflag::Aquatic | mobflag::Burns;
        def.tempt = "pufferfish_bucket,cod_bucket,salmon_bucket,tropical_fish_bucket,pufferfish,fish,salmon,clownfish";
        def.drops = "rotten_flesh:0:3:1";
        def.families = "aquatic,zombie_nautilus,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
