#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterGuardian(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:guardian";
        def.width = 0.85f;
        def.height = 0.85f;
        def.health = 30.0f;
        def.speed = 0.12f;
        def.damage = 5.0f;
        def.flags = mobflag::Hostile | mobflag::Aquatic;
        def.tempt = "";
        def.drops = "prismarine_shard:0:2:1;fish:1:1:1";
        def.families = "aquatic,guardian,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
