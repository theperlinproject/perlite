#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSheep(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:sheep";
        def.width = 0.9f;
        def.height = 1.3f;
        def.health = 8.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "wheat";
        def.drops = "muttonRaw:1:2:1";
        def.families = "sheep,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
