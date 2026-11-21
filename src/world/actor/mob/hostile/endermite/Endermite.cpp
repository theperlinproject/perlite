#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterEndermite(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:endermite";
        def.width = 0.4f;
        def.height = 0.3f;
        def.health = 8.0f;
        def.speed = 0.25f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "";
        def.families = "endermite,arthropod,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
