#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    namespace {
        class EndermanBehavior : public MobBehavior {
        public:
            bool AutoTargets(const Mob&) const override { return false; }
        };
    }

    void RegisterEnderman(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:enderman";
        def.width = 0.6f;
        def.height = 2.9f;
        def.health = 40.0f;
        def.speed = 0.3f;
        def.damage = 7.0f;
        def.flags = mobflag::Hostile;
        def.tempt = "";
        def.drops = "ender_pearl:0:1:1";
        def.families = "enderman,monster,mob";
        registry.Add(def, MobCategory::Hostile, std::make_unique<EndermanBehavior>());
    }
}
