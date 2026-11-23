#pragma once
#include "protocol/NbtTree.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace perlite::structure {
    // Reads a structure template file (big-endian NBT, gzipped or not) into a tree. The tags map onto
    // nbt::Value the same way as the little-endian reader's.
    bool ReadTemplateNbt(std::span<const uint8_t> file, nbt::Value& root);
    // Inflates a gzip stream; false when it is not one.
    bool Gunzip(std::span<const uint8_t> in, std::string& out);
}
