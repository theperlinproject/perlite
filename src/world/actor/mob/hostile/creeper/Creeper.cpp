#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    namespace {
        class CreeperBehavior : public MobBehavior {
        public:
            float ExplosionPower(const Mob&) const override { return 3.0f; }
        };
    }

    void RegisterCreeper(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:creeper";
        def.width = 0.6f;
        def.height = 1.8f;
        def.health = 20.0f;
        def.speed = 0.2f;
        def.damage = 3.0f;
        def.flags = mobflag::Hostile | mobflag::Explodes;
        def.tempt = "";
        def.drops = "gunpowder:0:2:1";
        def.families = "creeper,monster,mob";
        registry.Add(def, MobCategory::Hostile, std::make_unique<CreeperBehavior>());
    }
}
