#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSulfurCube(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:sulfur_cube";
        def.width = 0.98f;
        def.height = 0.98f;
        def.health = 4.0f;
        def.speed = 0.3f;
        def.damage = 0.0f;
        def.flags = mobflag::Explodes;
        def.tempt = "slime_ball";
        def.drops = "";
        def.families = "sulfur_cube,animal,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
