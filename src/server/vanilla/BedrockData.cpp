#include "server/vanilla/BedrockData.hpp"

#include "common/FileIo.hpp"
#include "common/Json.hpp"

#include <cstddef>
#include <cstring>
#include <span>

namespace perlite::bedrock_embedded {
    extern const unsigned char items_json[];
    extern const std::size_t items_json_size;
    extern const unsigned char item_components_bin[];
    extern const std::size_t item_components_bin_size;
    extern const unsigned char block_properties_bin[];
    extern const std::size_t block_properties_bin_size;
}

#ifndef PERLITE_BEDROCK_DIR
#define PERLITE_BEDROCK_DIR ""
#endif

namespace perlite {
    namespace fs = std::filesystem;

    namespace {
        bool ParseBlobs(std::span<const uint8_t> data, const std::string& label, std::vector<NamedNbt>& out,
                        std::string& error) {
            size_t at = 0;
            const auto take = [&](void* dst, size_t n) {
                if (at + n > data.size())
                    return false;
                std::memcpy(dst, data.data() + at, n);
                at += n;
                return true;
            };
            uint32_t count = 0;
            if (!take(&count, 4)) {
                error = label + " is truncated";
                return false;
            }
            out.clear();
            out.reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                uint16_t nameLen = 0;
                uint32_t nbtLen = 0;
                NamedNbt entry;
                if (!take(&nameLen, 2)) {
                    error = label + " is truncated";
                    return false;
                }
                entry.name.resize(nameLen);
                if (!take(entry.name.data(), nameLen) || !take(&nbtLen, 4)) {
                    error = label + " is truncated";
                    return false;
                }
                entry.nbt.resize(nbtLen);
                if (!take(entry.nbt.data(), nbtLen)) {
                    error = label + " is truncated";
                    return false;
                }
                out.push_back(std::move(entry));
            }
            return true;
        }

        bool ReadBlobs(const fs::path& path, std::vector<NamedNbt>& out, std::string& error) {
            std::vector<uint8_t> data;
            if (!file::ReadBytes(path, data)) {
                error = "cannot read " + path.string();
                return false;
            }
            return ParseBlobs(data, path.string(), out, error);
        }
    }

    BedrockData& BedrockData::Instance() {
        static BedrockData data;
        return data;
    }

    fs::path BedrockData::Find(const std::string& hint) {
        std::error_code ec;
        const char* env = std::getenv("PERLITE_BEDROCK_DATA");
        for (const std::string& base : {hint, env ? std::string(env) : std::string(), std::string("data"),
                                        std::string(PERLITE_BEDROCK_DIR), std::string("../data")}) {
            if (base.empty())
                continue;
            for (const char* sub : {"", "bedrock", "data/bedrock"}) {
                const fs::path dir = fs::path(base) / sub;
                if (fs::is_regular_file(dir / "items.json", ec))
                    return dir;
            }
        }
        return {};
    }

    bool BedrockData::ParseItems(const std::string& text, const std::string& label, std::string& error) {
        json::Value root;
        if (!json::Parse(text, root) || !root.IsArray()) {
            error = label + " is not a JSON array";
            return false;
        }
        items.clear();
        for (const json::Value& e : root.items) {
            VanillaItem item;
            item.name = e[0].String();
            item.networkId = static_cast<int16_t>(e[1].Int());
            item.version = static_cast<int32_t>(e[2].Int());
            item.componentBased = e[3].Int() != 0;
            if (!item.name.empty())
                items.push_back(std::move(item));
        }
        if (items.empty()) {
            error = label + " lists no items";
            return false;
        }
        return true;
    }

    bool BedrockData::Load(const fs::path& dir, std::string& error) {
        loaded = false;
        std::string text;
        if (!file::Read(dir / "items.json", text)) {
            error = "cannot read " + (dir / "items.json").string();
            return false;
        }
        if (!ParseItems(text, (dir / "items.json").string(), error) ||
            !ReadBlobs(dir / "item_components.bin", itemComponents, error) ||
            !ReadBlobs(dir / "block_properties.bin", blockProperties, error))
            return false;
        loaded = true;
        return true;
    }

    bool BedrockData::LoadBuiltin(std::string& error) {
        using namespace bedrock_embedded;
        loaded = false;
        const std::string text(reinterpret_cast<const char*>(items_json), items_json_size);
        if (!ParseItems(text, "the built-in items.json", error) ||
            !ParseBlobs({item_components_bin, item_components_bin_size}, "the built-in item_components.bin",
                        itemComponents, error) ||
            !ParseBlobs({block_properties_bin, block_properties_bin_size}, "the built-in block_properties.bin",
                        blockProperties, error))
            return false;
        loaded = true;
        return true;
    }
}
