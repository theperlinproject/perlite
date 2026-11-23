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

    // The structure templates and loot tables of the vanilla pack are not in bedrock-samples: they
    // come with the Bedrock Dedicated Server. When `download` is set and they are not there yet, the
    // latest Bedrock Dedicated Server is fetched from minecraft.net (its use is under the Minecraft
    // EULA, https://www.minecraft.net/eula) and only behavior_packs/vanilla/structures and
    // loot_tables are kept, in <data folder>/bds_vanilla_pack. Returns that folder, or empty.
    std::string EnsureStructures(const std::string& dataDir, bool download);
    // Keeps behavior_packs/vanilla/structures and loot_tables of a server zip in `target`.
    bool ExtractStructures(const std::string& zipBytes, const std::string& target, size_t& kept, std::string& error);
}
