#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterElderGuardian(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:elder_guardian";
        def.width = 1.99f;
        def.height = 1.99f;
        def.health = 80.0f;
        def.speed = 0.3f;
        def.damage = 5.0f;
        def.flags = mobflag::Hostile | mobflag::Aquatic;
        def.tempt = "";
        def.drops = "prismarine_shard:0:2:1;fish:1:1:1;wet_sponge:1:1:1;tide_armor_trim_smithing_template:1:1:1";
        def.families = "aquatic,guardian_elder,monster,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
