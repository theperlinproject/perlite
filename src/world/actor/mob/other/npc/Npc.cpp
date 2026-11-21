#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterNpc(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:npc";
        def.width = 0.6f;
        def.height = 2.1f;
        def.health = 10.0f;
        def.speed = 0.5f;
        def.damage = 0.0f;
        def.flags = 0u;
        def.tempt = "";
        def.drops = "";
        def.families = "npc,mob";
        registry.Add(def, MobCategory::Other);
    }
}
