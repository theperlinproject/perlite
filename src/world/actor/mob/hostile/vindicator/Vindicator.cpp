#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterVindicator(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:vindicator";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 24.0f;
        def.speed = 0.35f;
        def.damage = 8.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "emerald:0:1:1";
        def.families = "vindicator,monster,illager,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
