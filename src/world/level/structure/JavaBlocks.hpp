#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace perlite::structure {
    // A block state as structure templates store it: a name ("minecraft:oak_stairs") and its
    // properties, in the template's naming (the Bedrock vanilla pack keeps its templates in this
    // format, some with older names such as "planks" with a variant).
    struct JavaState {
        std::string name;
        std::vector<std::pair<std::string, std::string>> props;

        std::string Get(std::string_view key, std::string_view fallback = {}) const;
        void Set(std::string_view key, std::string_view value);
        std::string Text() const;
    };

    JavaState ParseState(std::string_view text);
    // The state turned clockwise by quarter turns (facing, axis, rotation, connections, rail shapes,
    // jigsaw orientation), as the game rotates blocks when it places a rotated template.
    JavaState Rotated(const JavaState& state, int quarterTurns);
    // The Bedrock block for a template state (cached; thread-safe). Air when nothing fits.
    uint16_t ToBedrock(const JavaState& state);
    // The Bedrock state text (for tests and logs).
    std::string BedrockText(const JavaState& state);
}
