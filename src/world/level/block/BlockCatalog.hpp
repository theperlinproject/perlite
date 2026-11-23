#pragma once
#include "world/level/block/Blocks.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace perlite {

    struct CatalogItem {
        std::string name;
        int16_t networkId = 0;
        int32_t blockHash = 0;
        int32_t version = 0;
        bool componentBased = false;

        bool block = true;
    };

    class BlockCatalog {
    public:
        static BlockCatalog& Instance();

        bool Load(const std::string& blockStatesPath, const std::string& itemNamesPath, std::string& error,
                  const std::string& extraItemsPath = {});

        void LoadBuiltin();
        void UseVanillaItems(const std::vector<CatalogItem>& vanilla);
        bool Loaded() const { return !items.empty(); }
        bool FromFiles() const { return fromFiles; }

        const std::vector<CatalogItem>& Items() const { return items; }
        const CatalogItem* ItemById(int16_t id) const;
        const CatalogItem* ItemByBlockHash(int32_t hash) const;
        const CatalogItem* ItemByName(const std::string& name) const;

        uint16_t RegistryId(int32_t hash) const;

        void SetCreativeEntries(std::vector<int16_t> networkIds) { creativeEntries = std::move(networkIds); }
        const CatalogItem* CreativeEntry(uint32_t entryId) const {
            if (creativeEntries.empty())
                return ItemById(static_cast<int16_t>(entryId));
            if (entryId == 0 || entryId > creativeEntries.size())
                return nullptr;
            return ItemById(creativeEntries[entryId - 1]);
        }
        uint32_t CreativeEntryOf(int16_t networkId) const {
            if (creativeEntries.empty())
                return static_cast<uint32_t>(static_cast<uint16_t>(networkId));
            for (size_t i = 0; i < creativeEntries.size(); i++)
                if (creativeEntries[i] == networkId)
                    return static_cast<uint32_t>(i + 1);
            return 0;
        }

        uint16_t DefaultBlock(const std::string& name) const;
        uint16_t Resolve(std::string_view state) const;
        std::string StateText(uint16_t id) const;
        bool KnownHash(int32_t hash) const { return states.contains(hash); }
        bool Knows(const std::string& fullName) const { return defaultByName.contains(fullName) || aliases.contains(fullName); }
        // A name that stands for another block or item (content mods' blocks and items, simulated as
        // their base). Thread-safe to read once registered before use.
        void Alias(const std::string& name, const std::string& target);
        void Unalias(const std::string& name);
        std::string Unaliased(const std::string& name) const;
        size_t StateCount() const { return states.size(); }

        static std::string FindFile(const std::string& dir, const std::string& name);

    private:
        struct State {
            std::string name;
            StateList list;
        };
        void BuildItems(const std::vector<std::string>& names,
                        const std::unordered_map<std::string, int32_t>& defaultHash);
        std::unordered_map<int32_t, State> states;
        std::vector<CatalogItem> items;
        std::vector<int16_t> creativeEntries;
        std::unordered_map<std::string, int32_t> defaultByName;
        std::unordered_map<std::string, std::string> aliases;
        std::unordered_map<int16_t, size_t> byId;
        std::unordered_map<int32_t, size_t> byHash;
        std::unordered_map<std::string, size_t> byName;
        bool fromFiles = false;
        mutable std::mutex resolveMutex;
        mutable std::unordered_map<std::string, uint16_t> resolved;
        mutable std::unordered_map<std::string, std::vector<int32_t>> hashesByName;
    };
}
