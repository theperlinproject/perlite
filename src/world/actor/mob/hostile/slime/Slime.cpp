#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSlime(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:slime";
        def.width = 2.08f;
        def.height = 2.08f;
        def.health = 16.0f;
        def.speed = 0.6f;
        def.damage = 4.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "slime_ball:0:2:1";
        def.families = "slime,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
