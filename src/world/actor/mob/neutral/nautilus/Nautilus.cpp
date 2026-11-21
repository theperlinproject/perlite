#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterNautilus(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:nautilus";
        def.width = 0.44f;
        def.height = 0.5f;
        def.health = 15.0f;
        def.speed = 0.15f;
        def.damage = 3.0f;
        def.flags = mobflag::Panic | mobflag::Aquatic | mobflag::Neutral;
        def.tempt = "pufferfish_bucket,cod_bucket,salmon_bucket,tropical_fish_bucket,pufferfish,fish,salmon,clownfish";
        def.drops = "nautilus_shell:1:1:0.05";
        def.families = "aquatic,nautilus,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
