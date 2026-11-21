#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterParrot(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:parrot";
        def.width = 0.5f;
        def.height = 1.0f;
        def.health = 6.0f;
        def.speed = 0.4f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Fly;
        def.tempt = "";
        def.drops = "feather:1:2:1";
        def.families = "parrot_tame,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
