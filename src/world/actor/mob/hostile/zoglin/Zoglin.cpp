#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterZoglin(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:zoglin";
        def.width = 1.4f;
        def.height = 1.4f;
        def.health = 40.0f;
        def.speed = 0.25f;
        def.damage = 8.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "rotten_flesh:1:3:1";
        def.families = "zoglin,zoglin_adult,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
