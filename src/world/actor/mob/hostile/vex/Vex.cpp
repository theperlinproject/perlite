#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterVex(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:vex";
        def.width = 0.4f;
        def.height = 0.8f;
        def.health = 14.0f;
        def.speed = 1.0f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "";
        def.families = "vex,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
