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
        // From the vanilla behavior pack when it is found: how the client scales the model, and the
        // baby's own box and scale (0: half the adult's).
        float scale = 1.0f, babyScale = 0.0f, babyWidth = 0.0f, babyHeight = 0.0f;
        // A content mod's entity: the entity type clients are shown (null: id), and the vanilla
        // type whose behaviour it borrows.
        const char* wireId = nullptr;
        const char* baseId = nullptr;
    };

    const MobDef* FindMob(const char* identifier);
}
