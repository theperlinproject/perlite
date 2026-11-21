#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterRavager(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:ravager";
        def.width = 1.95f;
        def.height = 2.2f;
        def.health = 100.0f;
        def.speed = 0.0f;
        def.damage = 12.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "saddle:1:1:1";
        def.families = "monster,ravager,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
