#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterEnderDragon(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:ender_dragon";
        def.width = 13.0f;
        def.height = 4.0f;
        def.health = 200.0f;
        def.speed = 0.3f;
        def.damage = 3.0f;
        def.flags = mobflag::Ranged;
        def.tempt = "";
        def.drops = "";
        def.families = "dragon,mob";
        registry.Add(def, MobCategory::Boss);
    }
}
