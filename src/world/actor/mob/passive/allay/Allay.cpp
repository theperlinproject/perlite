#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterAllay(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:allay";
        def.width = 0.35f;
        def.height = 0.6f;
        def.health = 20.0f;
        def.speed = 0.1f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Fly;
        def.tempt = "";
        def.drops = "";
        def.families = "allay,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
