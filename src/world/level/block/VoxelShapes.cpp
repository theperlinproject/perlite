#include "world/level/block/VoxelShapes.hpp"

#include "common/FileIo.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace perlite {
    namespace fs = std::filesystem;

    VoxelShapes& VoxelShapes::Instance() {
        static VoxelShapes shapes;
        return shapes;
    }

    bool VoxelShapes::ParseShape(const std::string& text, std::string& identifier, std::vector<VoxelBox>& boxes) {
        json::Value root;
        if (!json::Parse(text, root))
            return false;
        const json::Value& shape = root["minecraft:voxel_shape"];
        if (!shape.IsObject())
            return false;
        identifier = shape["description"]["identifier"].String();
        if (identifier.empty())
            return false;
        boxes.clear();
        const json::Value& list = shape["shape"]["boxes"];
        for (size_t i = 0; i < list.Size(); i++) {
            const json::Value& box = list[i];
            const json::Value& lo = box["min"];
            const json::Value& hi = box["max"];
            if (lo.Size() != 3 || hi.Size() != 3)
                return false;
            VoxelBox b;
            for (size_t axis = 0; axis < 3; axis++) {
                b.min[axis] = static_cast<float>(lo[axis].Number() / 16.0);
                b.max[axis] = static_cast<float>(hi[axis].Number() / 16.0);
                if (b.min[axis] > b.max[axis])
                    std::swap(b.min[axis], b.max[axis]);
            }
            boxes.push_back(b);
        }
        return true;
    }

    proto::VoxelShape VoxelShapes::ToCells(const std::vector<VoxelBox>& boxes) {
        proto::VoxelShape shape;
        if (boxes.empty()) {
            shape.xCoordinates = {0.0f};
            shape.yCoordinates = {0.0f};
            shape.zCoordinates = {0.0f};
            return shape;
        }
        std::vector<float> edges[3];
        for (size_t axis = 0; axis < 3; axis++) {
            std::set<float> set;
            for (const VoxelBox& b : boxes) {
                set.insert(b.min[axis]);
                set.insert(b.max[axis]);
            }
            edges[axis].assign(set.begin(), set.end());
        }
        const size_t nx = edges[0].size() - 1, ny = edges[1].size() - 1, nz = edges[2].size() - 1;
        const auto inside = [&](float x, float y, float z) {
            for (const VoxelBox& b : boxes)
                if (x >= b.min[0] && x <= b.max[0] && y >= b.min[1] && y <= b.max[1] && z >= b.min[2] &&
                    z <= b.max[2])
                    return true;
            return false;
        };
        shape.cells.xSize = static_cast<uint8_t>(nx);
        shape.cells.ySize = static_cast<uint8_t>(ny);
        shape.cells.zSize = static_cast<uint8_t>(nz);
        // One bit per cell, z varying fastest, then y, then x.
        shape.cells.storage.assign((nx * ny * nz + 7) / 8, 0);
        for (size_t x = 0; x < nx; x++)
            for (size_t y = 0; y < ny; y++)
                for (size_t z = 0; z < nz; z++) {
                    const float mx = (edges[0][x] + edges[0][x + 1]) / 2;
                    const float my = (edges[1][y] + edges[1][y + 1]) / 2;
                    const float mz = (edges[2][z] + edges[2][z + 1]) / 2;
                    if (!inside(mx, my, mz))
                        continue;
                    const size_t bit = z + y * nz + x * nz * ny;
                    shape.cells.storage[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
                }
        shape.xCoordinates = std::move(edges[0]);
        shape.yCoordinates = std::move(edges[1]);
        shape.zCoordinates = std::move(edges[2]);
        return shape;
    }

    size_t VoxelShapes::Load(const std::string& behaviorPackDir) {
        packet = {};
        std::error_code ec;
        const fs::path dir = fs::path(behaviorPackDir) / "shapes";
        if (behaviorPackDir.empty() || !fs::is_directory(dir, ec))
            return 0;
        std::map<std::string, std::vector<VoxelBox>> byName;
        size_t failed = 0;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".json")
                continue;
            std::string text, identifier;
            std::vector<VoxelBox> boxes;
            if (!file::Read(e.path(), text) || !ParseShape(text, identifier, boxes)) {
                failed++;
                continue;
            }
            byName[identifier] = std::move(boxes);
        }
        if (failed)
            LogWarn("Vanilla behavior pack: " + std::to_string(failed) + " shape file(s) could not be read");
        for (const auto& [name, boxes] : byName) {
            packet.nameMap.push_back({name, static_cast<uint16_t>(packet.shapes.size())});
            packet.shapes.push_back(ToCells(boxes));
        }
        packet.customShapeCount = 0;
        return byName.size();
    }
}
