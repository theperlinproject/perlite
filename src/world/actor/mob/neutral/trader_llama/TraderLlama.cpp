#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterTraderLlama(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:trader_llama";
        def.width = 0.9f;
        def.height = 1.87f;
        def.health = 30.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = mobflag::Panic | mobflag::Ranged | mobflag::Neutral;
        def.tempt = "hay_block";
        def.drops = "leather:0:2:1";
        def.families = "trader_llama,llama,mob";
        registry.Add(def, MobCategory::Neutral);
    }
}
