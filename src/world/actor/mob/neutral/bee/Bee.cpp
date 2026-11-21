#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterBee(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:bee";
        def.width = 0.55f;
        def.height = 0.5f;
        def.health = 10.0f;
        def.speed = 0.3f;
        def.damage = 2.0f;
        def.flags = mobflag::Panic | mobflag::Fly | mobflag::Neutral;
        def.tempt = "poppy,blue_orchid,allium,azure_bluet,red_tulip,orange_tulip,white_tulip,pink_tulip";
        def.drops = "";
        def.families = "bee,mob,arthropod";
        registry.Add(def, MobCategory::Neutral);
    }
}
