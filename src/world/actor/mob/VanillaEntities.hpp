#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace perlite {

    // Mob data read from the vanilla behavior pack (the entities folder of Mojang's vanilla
    // behavior pack, downloaded at runtime). It is the reference for sizes, scale, health and speed, so a mob's
    // hitbox and the way the client draws it match the game.
    struct VanillaSize {
        int variant = 0;    // the "minecraft:variant" the client draws the size from (slimes: 1, 2, 4)
        float width = 0, height = 0;
        float health = 0;   // 0: the mob's normal health
        float damage = -1;  // -1: the mob's normal attack damage
    };

    struct VanillaEntity {
        std::string identifier;
        float width = 0, height = 0;           // adult collision box (0: not given)
        float scale = 1;                       // adult "minecraft:scale"
        float babyWidth = 0, babyHeight = 0;   // baby collision box (0: half the adult box)
        float babyScale = 0;                   // baby "minecraft:scale" (0: not given)
        float health = 0;                      // max health (0: not given)
        float speed = 0;                       // "minecraft:movement" (0: not given)
        float damage = -1;                     // "minecraft:attack" damage (-1: not given)
        std::vector<VanillaSize> sizes;        // size classes (slime, magma cube)
        std::vector<int> variants;             // appearance variants picked at spawn (rabbit coats)
    };

    class VanillaEntities {
    public:
        static VanillaEntities& Instance();

        // Reads every entity file under <dir>/entities. Returns how many were read.
        size_t Load(const std::string& behaviorPackDir);

        const VanillaEntity* Get(const std::string& identifier) const;
        size_t Count() const { return entities.size(); }

        // Parses one entity file (exposed for tests). False when it is not an entity.
        static bool ParseEntity(const std::string& text, VanillaEntity& out);

    private:
        std::unordered_map<std::string, VanillaEntity> entities;
    };
}
