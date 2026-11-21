#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterAxolotl(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:axolotl";
        def.width = 0.75f;
        def.height = 0.42f;
        def.health = 14.0f;
        def.speed = 0.1f;
        def.damage = 2.0f;
        def.flags = mobflag::Hostile | mobflag::Aquatic;
        def.tempt = "tropical_fish_bucket";
        def.drops = "";
        def.families = "aquatic,axolotl,mob";
        registry.Add(def, MobCategory::Hostile);
    }
}
