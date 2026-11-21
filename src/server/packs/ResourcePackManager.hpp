#pragma once
#include "protocol/Packets.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace perlite {

    struct ResourcePack {
        std::string uuid;
        std::string version;
        std::string name;
        std::string contentKey;
        std::vector<uint8_t> archive;
        std::vector<uint8_t> sha256;
        bool behavior = false;
        bool hasScripts = false;
    };

    class ResourcePackManager {
    public:
        static constexpr uint32_t kChunkSize = 256 * 1024;

        void Load(const std::string& dataDir, bool required);
        const std::vector<ResourcePack>& Packs() const { return packs; }
        bool Required() const { return required; }

        const ResourcePack* Find(const std::string& packId) const;
        void FillInfo(proto2225::ResourcePacksInfoPacket& info) const;
        void FillStack(proto2225::ResourcePackStackPacket& stack) const;
        bool DataInfo(const std::string& packId, proto2225::ResourcePackDataInfoPacket& out) const;
        bool Chunk(const std::string& packId, uint32_t index,
                   proto2225::ResourcePackChunkDataPacket& out) const;

        static bool ReadPack(const std::string& path, ResourcePack& out, std::string& error);

    private:
        std::vector<ResourcePack> packs;
        bool required = false;
    };
}
