#include "world/actor/mob/MobRegistry.hpp"

namespace perlite::mobs {
    void RegisterTripodCamera(MobRegistry& registry) {
        MobDef def{};
        def.id = "minecraft:tripod_camera";
        def.width = 0.75f;
        def.height = 1.8f;
        def.health = 4.0f;
        def.speed = 0.25f;
        def.damage = 0.0f;
        def.flags = 0u;
        def.tempt = "";
        def.drops = "";
        def.families = "tripodcamera,inanimate,mob";
        registry.Add(def, MobCategory::Other);
    }
}
