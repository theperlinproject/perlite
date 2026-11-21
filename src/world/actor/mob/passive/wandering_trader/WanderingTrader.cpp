#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWanderingTrader(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:wandering_trader";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 20.0f;
        def.speed = 0.5f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "";
        def.drops = "";
        def.families = "wandering_trader,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
