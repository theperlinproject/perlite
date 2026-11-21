#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterHappyGhast(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:happy_ghast";
        def.width = 4.0f;
        def.height = 4.0f;
        def.health = 20.0f;
        def.speed = 0.3f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Fly;
        def.tempt = "snowball";
        def.drops = "";
        def.families = "happy_ghast,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
