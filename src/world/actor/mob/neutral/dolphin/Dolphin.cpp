#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterDolphin(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:dolphin";
        def.width = 0.9f;
        def.height = 0.6f;
        def.health = 10.0f;
        def.speed = 0.1f;
        def.damage = 3.0f;
        def.flags = mobflag::Panic | mobflag::Aquatic | mobflag::Neutral;
        def.tempt = "";
        def.drops = "fish:0:1:1";
        def.families = "aquatic,dolphin,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
