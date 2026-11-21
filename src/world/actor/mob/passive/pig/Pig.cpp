#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPig(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:pig";
        def.width = 0.9f;
        def.height = 0.9f;
        def.health = 10.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "potato,carrot,beetroot,carrotOnAStick";
        def.drops = "porkchop:1:3:1";
        def.families = "pig,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
