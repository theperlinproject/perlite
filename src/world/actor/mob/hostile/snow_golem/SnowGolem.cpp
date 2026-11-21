#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSnowGolem(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:snow_golem";
        def.width = 0.4f;
        def.height = 1.8f;
        def.health = 4.0f;
        def.speed = 0.2f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "snowball:0:15:1";
        def.families = "snowgolem,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
