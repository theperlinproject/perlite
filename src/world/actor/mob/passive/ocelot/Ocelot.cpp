#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterOcelot(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:ocelot";
        def.width = 0.6f;
        def.height = 0.7f;
        def.health = 10.0f;
        def.speed = 0.3f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "fish,salmon";
        def.drops = "";
        def.families = "ocelot,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
