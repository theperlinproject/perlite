#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterPhantom(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:phantom";
        def.width = 0.9f;
        def.height = 0.5f;
        def.health = 20.0f;
        def.speed = 1.8f;
        def.damage = 6.0f;
        def.flags = mobflag::Hostile | mobflag::Burns;
        def.tempt = "";
        def.drops = "phantom_membrane:0:1:1";
        def.families = "phantom,undead,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
