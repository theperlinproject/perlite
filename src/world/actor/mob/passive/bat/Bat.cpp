#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterBat(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:bat";
        def.width = 0.5f;
        def.height = 0.9f;
        def.health = 6.0f;
        def.speed = 0.1f;
        def.damage = 0.0f;
        def.flags = mobflag::Fly;
        def.tempt = "";
        def.drops = "";
        def.families = "bat,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
