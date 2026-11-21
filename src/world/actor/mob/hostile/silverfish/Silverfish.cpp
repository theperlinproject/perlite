#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSilverfish(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:silverfish";
        def.width = 0.4f;
        def.height = 0.3f;
        def.health = 8.0f;
        def.speed = 0.25f;
        def.damage = 1.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "";
        def.families = "silverfish,monster,mob,arthropod";
        registry.Add(def, MobCategory::Hostile);
    }
}
