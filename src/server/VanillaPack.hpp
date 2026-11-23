#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <string>

namespace perlite::vanillapack {

    // Mojang's vanilla behavior pack is not shipped with Perlite. On first start the latest
    // Bedrock Dedicated Server is downloaded from minecraft.net (its use is under the Minecraft
    // EULA, https://www.minecraft.net/eula) and only its behavior packs are kept: the vanilla pack
    // and the versioned packs stacked on it (vanilla_1.14 ... the newest), merged in version order
    // the way the game stacks them, with the packed .brarchive files unpacked. The result lives in
    // <data folder>/vanilla_behavior_pack (entities, shapes, recipes, structures, loot_tables,
    // spawn rules, biomes, features and the rest); the server download itself is deleted.

    // The folder to read the pack from: $PERLITE_VANILLA_PACK when set, otherwise the kept copy,
    // downloading it first when it is missing or older than the latest server (and download is
    // true). Empty when there is no pack.
    std::string Ensure(const std::string& dataDir, bool download);

    // Whether a pack folder has structure templates and loot tables.
    bool HasStructures(const std::string& dir);

    // Builds the merged pack in `target` from a Bedrock Dedicated Server zip on disk. `kept` is
    // the number of files written.
    bool ExtractPack(const std::string& zipPath, const std::string& target, size_t& kept, std::string& error);

    // The entries of a .brarchive (the game's packed folder): calls `each` with every file's name
    // and contents. False when the bytes are not an archive.
    bool ReadArchive(const std::string& bytes, const std::function<void(const std::string& name, std::string_view data)>& each);
}
