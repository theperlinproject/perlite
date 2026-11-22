#pragma once

#include <string>

namespace perlite::vanillapack {

    // Mojang's vanilla behavior pack is not shipped with Perlite. It is downloaded from
    // Mojang/bedrock-samples on GitHub the first time the server starts, into
    // <data folder>/vanilla_behavior_pack, and read from there after that.
    inline constexpr const char* kRepository = "Mojang/bedrock-samples";
    inline constexpr const char* kRef = "v1.26.60.5";

    // The folder to read the pack from: $PERLITE_VANILLA_PACK when set, otherwise the downloaded copy,
    // downloading it first when it is missing or from another version (and download is true).
    // Empty when there is no pack.
    std::string Ensure(const std::string& dataDir, bool download);
}
