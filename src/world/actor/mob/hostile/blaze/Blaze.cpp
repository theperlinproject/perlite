#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterBlaze(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:blaze";
        def.width = 0.5f;
        def.height = 1.8f;
        def.health = 20.0f;
        def.speed = 0.23f;
        def.damage = 6.0f;
        def.flags = mobflag::Hostile | mobflag::Ranged;
        def.tempt = "";
        def.drops = "blaze_rod:0:1:1";
        def.families = "blaze,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
