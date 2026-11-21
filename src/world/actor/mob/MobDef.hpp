#pragma once
#include <cstdint>

namespace perlite {

    namespace mobflag {
        constexpr uint32_t Hostile = 1, Panic = 2, Fly = 4, Aquatic = 8, Burns = 16, Explodes = 32, Ranged = 64,
                           Neutral = 128, BabyOk = 256, Spider = 512;
    }

    struct MobDef {
        const char* id = "";
        float width = 0.6f, height = 1.8f, health = 10.0f, speed = 0.25f, damage = 0.0f;
        uint32_t flags = 0;
        const char* tempt = "";
        const char* drops = "";
        const char* families = "";
    };

    const MobDef* FindMob(const char* identifier);
}
