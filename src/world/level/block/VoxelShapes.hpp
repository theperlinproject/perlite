#pragma once

#include "protocol/Packets.hpp"

#include <string>

namespace perlite {

    // Block shapes from the shapes folder of the vanilla behavior pack (downloaded at runtime). Each shape is a
    // list of boxes in 1/16 block units; the client looks them up by name for collision and the outline
    // of blocks that are not full cubes, so they are sent once when a player joins.
    struct VoxelBox {
        float min[3]{};
        float max[3]{};
    };

    class VoxelShapes {
    public:
        static VoxelShapes& Instance();

        // Reads every shape under <behaviorPackDir>/shapes. Returns how many were read.
        size_t Load(const std::string& behaviorPackDir);
        size_t Count() const { return packet.nameMap.size(); }

        const proto::VoxelShapesPacket& Packet() const { return packet; }

        // Parses one shape file (exposed for tests). Boxes come back in block units (0..1).
        static bool ParseShape(const std::string& text, std::string& identifier, std::vector<VoxelBox>& boxes);
        // The grid form the client reads: the distinct box edges on each axis, and one bit per cell
        // telling whether the cell is solid.
        static proto::VoxelShape ToCells(const std::vector<VoxelBox>& boxes);

    private:
        proto::VoxelShapesPacket packet;
    };
}
