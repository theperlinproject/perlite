#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterTadpole(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:tadpole";
        def.width = 0.8f;
        def.height = 0.6f;
        def.health = 6.0f;
        def.speed = 0.1f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Aquatic;
        def.tempt = "slime_ball";
        def.drops = "";
        def.families = "aquatic,tadpole,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
