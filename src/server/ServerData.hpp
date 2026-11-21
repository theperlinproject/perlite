#pragma once
#include "protocol/Packets.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace perlite::serverdata {

    struct EntityType {
        std::string id;
        int32_t legacyId = 0;
        bool spawnEgg = false;
        bool summonable = true;
    };

    const std::vector<EntityType>& EntityTypes();
    const std::vector<uint8_t>& EntityIdentifiersNbt();
    const std::vector<uint8_t>& JigsawStructureDataNbt();
    const proto2225::BiomeDefinitionListPacket& BiomeDefinitions();
    int32_t LegacyEntityId(const std::string& identifier);
}
