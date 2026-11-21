#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSquid(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:squid";
        def.width = 0.8f;
        def.height = 0.8f;
        def.health = 10.0f;
        def.speed = 0.2f;
        def.damage = 0.0f;
        def.flags = mobflag::Aquatic;
        def.tempt = "";
        def.drops = "dye:1:3:1";
        def.families = "aquatic,squid,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
