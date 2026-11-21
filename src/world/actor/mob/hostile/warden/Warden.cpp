#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterWarden(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:warden";
        def.width = 0.9f;
        def.height = 2.9f;
        def.health = 500.0f;
        def.speed = 0.3f;
        def.damage = 30.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "sculk_catalyst:1:1:1";
        def.families = "warden,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
