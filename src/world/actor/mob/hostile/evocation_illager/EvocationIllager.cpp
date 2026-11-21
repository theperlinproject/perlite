#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterEvocationIllager(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:evocation_illager";
        def.width = 0.6f;
        def.height = 1.9f;
        def.health = 24.0f;
        def.speed = 0.5f;
        def.damage = 0.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "emerald:0:1:1;totem:1:1:1";
        def.families = "evocation_illager,monster,illager,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
