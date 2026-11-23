#pragma once
#include "protocol/NbtTree.hpp"
#include "world/level/structure/JavaBlocks.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace perlite::structure {
    // A jigsaw connector in a template: where it is, which way it faces (front) and which way is its
    // top, the name other jigsaws look for, the name it looks for, the pool it pulls pieces from and
    // what it turns into once the structure is placed.
    struct Jigsaw {
        int x = 0, y = 0, z = 0;
        std::string front = "north", top = "up";
        std::string name, target, pool, finalState = "minecraft:air";
        bool rollable = false;
        int selectionPriority = 0;
    };

    struct TemplateBlock {
        int16_t x = 0, y = 0, z = 0;
        uint16_t state = 0; // index into the palette
        int32_t nbt = -1;   // index into Template::nbts
    };

    // A data marker (structure block in data mode): what the game spawns or fills there.
    struct DataMarker {
        int x = 0, y = 0, z = 0;
        std::string text;
    };

    struct TemplateEntity {
        double x = 0, y = 0, z = 0;
        std::string id;
    };

    struct Template {
        std::string name;
        int sx = 0, sy = 0, sz = 0;
        std::vector<JavaState> palette;
        std::vector<TemplateBlock> blocks; // ordered by y, then x, then z
        std::vector<nbt::Value> nbts;
        std::vector<Jigsaw> jigsaws; // in block order
        std::vector<DataMarker> markers;
        std::vector<TemplateEntity> entities;
        std::vector<bool> isAir, isJigsaw, isStructureBlock; // per palette entry

        // The palette as Bedrock blocks, for each quarter turn.
        const std::vector<uint16_t>& Bedrock(int turns) const;

    private:
        mutable std::array<std::vector<uint16_t>, 4> bedrock;
        mutable std::array<std::once_flag, 4> translated;
    };

    // Templates read from a Bedrock vanilla behavior pack's structures folder, and the pools its
    // jigsaws name: a pool is the folder of that name (its templates, in name order), and its
    // fallback is the terminators folder beside it when there is one.
    class TemplateStore {
    public:
        // Finds the pack: `configured` (a pack folder or its structures folder), else
        // behavior_packs/vanilla beside the working folder or the data folder. Null when none.
        static std::shared_ptr<TemplateStore> Find(const std::string& configured, const std::string& dataDir);
        explicit TemplateStore(std::string structuresDir) : dir(std::move(structuresDir)) {}

        const std::string& Directory() const { return dir; }
        std::shared_ptr<const Template> Get(std::string_view name) const;
        const std::vector<std::string>& Pool(std::string_view pool) const;
        std::string Fallback(std::string_view pool) const;
        // The tallest template in a pool (and its fallback), for the game's village expansion rule.
        int MaxHeight(std::string_view pool) const;

    private:
        std::string dir;
        mutable std::mutex mutex;
        mutable std::map<std::string, std::shared_ptr<const Template>> templates;
        mutable std::map<std::string, std::vector<std::string>> pools;
        mutable std::map<std::string, int> heights;
    };

    std::string StripNamespace(std::string_view s);
}
