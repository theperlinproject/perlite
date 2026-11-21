#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterStrider(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:strider";
        def.width = 0.9f;
        def.height = 1.7f;
        def.health = 20.0f;
        def.speed = 0.16f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "warped_fungus,warped_fungus_on_a_stick";
        def.drops = "string:2:5:1;saddle:1:1:1";
        def.families = "strider,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
