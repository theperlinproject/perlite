#include "server/vanilla/BedrockData.hpp"

#include "common/FileIo.hpp"
#include "common/Json.hpp"

#include <cstring>

#ifndef PERLITE_BEDROCK_DIR
#define PERLITE_BEDROCK_DIR ""
#endif

namespace perlite {
    namespace fs = std::filesystem;

    namespace {
        bool ReadBlobs(const fs::path& path, std::vector<NamedNbt>& out, std::string& error) {
            std::vector<uint8_t> data;
            if (!file::ReadBytes(path, data)) {
                error = "cannot read " + path.string();
                return false;
            }
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
                error = path.string() + " is truncated";
                return false;
            }
            out.clear();
            out.reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                uint16_t nameLen = 0;
                uint32_t nbtLen = 0;
                NamedNbt entry;
                if (!take(&nameLen, 2)) {
                    error = path.string() + " is truncated";
                    return false;
                }
                entry.name.resize(nameLen);
                if (!take(entry.name.data(), nameLen) || !take(&nbtLen, 4)) {
                    error = path.string() + " is truncated";
                    return false;
                }
                entry.nbt.resize(nbtLen);
                if (!take(entry.nbt.data(), nbtLen)) {
                    error = path.string() + " is truncated";
                    return false;
                }
                out.push_back(std::move(entry));
            }
            return true;
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

    bool BedrockData::Load(const fs::path& dir, std::string& error) {
        std::string text;
        if (!file::Read(dir / "items.json", text)) {
            error = "cannot read " + (dir / "items.json").string();
            return false;
        }
        json::Value root;
        if (!json::Parse(text, root) || !root.IsArray()) {
            error = (dir / "items.json").string() + " is not a JSON array";
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
        if (!ReadBlobs(dir / "item_components.bin", itemComponents, error) ||
            !ReadBlobs(dir / "block_properties.bin", blockProperties, error))
            return false;

        loaded = !items.empty();
        if (!loaded)
            error = (dir / "items.json").string() + " lists no items";
        return loaded;
    }
}
