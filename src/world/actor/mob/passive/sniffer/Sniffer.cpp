#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterSniffer(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:sniffer";
        def.width = 1.9f;
        def.height = 1.75f;
        def.health = 14.0f;
        def.speed = 0.09f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic;
        def.tempt = "torchflower_seeds";
        def.drops = "";
        def.families = "sniffer,mob";
        registry.Add(def, MobCategory::Passive);
    }
}
