#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCaveSpider(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:cave_spider";
        def.width = 0.7f;
        def.height = 0.5f;
        def.health = 12.0f;
        def.speed = 0.3f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Spider;
        def.tempt = "";
        def.drops = "string:0:2:1;spider_eye:0:1:1";
        def.families = "cavespider,monster,arthropod,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
