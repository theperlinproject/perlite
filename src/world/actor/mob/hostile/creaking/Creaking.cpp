#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterCreaking(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:creaking";
        def.width = 0.9f;
        def.height = 2.7f;
        def.health = 1.0f;
        def.speed = 0.4f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "";
        def.families = "creaking,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
