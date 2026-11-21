#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterBreeze(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:breeze";
        def.width = 0.6f;
        def.height = 1.77f;
        def.health = 30.0f;
        def.speed = 0.4f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "breeze_rod:1:2:1";
        def.families = "breeze,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
