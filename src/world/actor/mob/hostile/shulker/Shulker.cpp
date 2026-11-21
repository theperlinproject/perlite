#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterShulker(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:shulker";
        def.width = 0.6f;
        def.height = 1.8f;
        def.health = 30.0f;
        def.speed = 0.0f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "shulker_shell:0:1:1";
        def.families = "shulker,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
