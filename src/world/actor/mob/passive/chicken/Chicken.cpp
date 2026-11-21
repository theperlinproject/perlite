#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterChicken(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:chicken";
        def.width = 0.6f;
        def.height = 0.8f;
        def.health = 4.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "wheat_seeds,beetroot_seeds,melon_seeds,pumpkin_seeds,pitcher_pod,torchflower_seeds";
        def.drops = "feather:0:2:1;chicken:1:1:1";
        def.families = "chicken,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
