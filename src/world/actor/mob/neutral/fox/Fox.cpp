#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterFox(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:fox";
        def.width = 0.6f;
        def.height = 0.7f;
        def.health = 10.0f;
        def.speed = 0.3f;
        def.damage = 2.0f;
        def.flags = mobflag::Panic | mobflag::Neutral;
        def.tempt = "sweet_berries,glow_berries";
        def.drops = "";
        def.families = "fox,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
