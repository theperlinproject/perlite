#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSpider(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:spider";
        def.width = 1.4f;
        def.height = 0.9f;
        def.health = 16.0f;
        def.speed = 0.3f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Spider;
        def.tempt = "";
        def.drops = "string:0:2:1;spider_eye:0:1:1";
        def.families = "spider,monster,mob,arthropod";
        registry.Add(def, MobCategory::Hostile);
    }
}
