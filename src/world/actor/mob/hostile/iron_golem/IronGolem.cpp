#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterIronGolem(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:iron_golem";
        def.width = 1.4f;
        def.height = 2.9f;
        def.health = 100.0f;
        def.speed = 0.25f;
        def.damage = 21.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "poppy:0:2:1;iron_ingot:3:5:1";
        def.families = "irongolem,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
