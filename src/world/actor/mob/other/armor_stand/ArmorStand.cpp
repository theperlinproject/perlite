#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterArmorStand(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:armor_stand";
        def.width = 0.5f;
        def.height = 1.975f;
        def.health = 6.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = 0u;
        def.tempt = "";
        def.drops = "";
        def.families = "armor_stand,inanimate,mob";
        registry.Add(def, MobCategory::Other);
    }
}
