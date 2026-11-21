#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterMagmaCube(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:magma_cube";
        def.width = 2.08f;
        def.height = 2.08f;
        def.health = 16.0f;
        def.speed = 0.75f;
        def.damage = 6.0f;
        def.flags = mobflag::Hostile | mobflag::Burns;
        def.tempt = "";
        def.drops = "magma_cream:0:1:1";
        def.families = "magmacube,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
