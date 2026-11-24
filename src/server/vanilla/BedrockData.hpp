#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace perlite {

    struct NamedNbt {
        std::string name;
        std::vector<uint8_t> nbt;
    };

    struct VanillaItem {
        std::string name;
        int16_t networkId = 0;
        int32_t version = 0;
        bool componentBased = false;
    };

    class BedrockData {
    public:
        static BedrockData& Instance();

        bool Load(const std::filesystem::path& dir, std::string& error);
        // The copy compiled into Perlite at build time, for when no data/bedrock folder is found.
        bool LoadBuiltin(std::string& error);
        bool Loaded() const { return loaded; }

        const std::vector<VanillaItem>& Items() const { return items; }
        const std::vector<NamedNbt>& ItemComponents() const { return itemComponents; }
        const std::vector<NamedNbt>& BlockProperties() const { return blockProperties; }

        static std::filesystem::path Find(const std::string& hint);

    private:
        bool ParseItems(const std::string& text, const std::string& label, std::string& error);

        bool loaded = false;
        std::vector<VanillaItem> items;
        std::vector<NamedNbt> itemComponents;
        std::vector<NamedNbt> blockProperties;
    };
}
