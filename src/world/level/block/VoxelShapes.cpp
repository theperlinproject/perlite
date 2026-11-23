#include "world/level/block/VoxelShapes.hpp"

#include <cstring>

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

    namespace {
        // The dedicated server's packs keep shapes in the game's compiled form: "\x7fMCB", a
        // 2-byte version, a 2-byte schema and a 4-byte format, then the definition's fields in
        // order: the type name and the identifier (varint-length strings), the box count (varint)
        // and six floats per box (min x, y, z, max x, y, z) in sixteenths.
        bool ParseCompiledShape(const std::string& d, std::string& identifier, std::vector<VoxelBox>& boxes) {
            size_t p = 12;
            const auto varint = [&](uint64_t& out) {
                out = 0;
                for (int shift = 0; shift < 35; shift += 7) {
                    if (p >= d.size())
                        return false;
                    const uint8_t b = static_cast<uint8_t>(d[p++]);
                    out |= static_cast<uint64_t>(b & 0x7F) << shift;
                    if (!(b & 0x80))
                        return true;
                }
                return false;
            };
            uint64_t len = 0;
            if (!varint(len) || p + len > d.size())
                return false;
            const std::string type = d.substr(p, len);
            p += len;
            if (type != "minecraft:voxel_shape" || !varint(len) || p + len > d.size())
                return false;
            identifier = d.substr(p, len);
            p += len;
            uint64_t count = 0;
            if (!varint(count) || p + count * 24 != d.size())
                return false;
            boxes.clear();
            for (uint64_t i = 0; i < count; i++) {
                VoxelBox b;
                std::memcpy(b.min, d.data() + p, 12);
                std::memcpy(b.max, d.data() + p + 12, 12);
                p += 24;
                for (int axis = 0; axis < 3; axis++) {
                    b.min[axis] /= 16.0f;
                    b.max[axis] /= 16.0f;
                    if (b.min[axis] > b.max[axis])
                        std::swap(b.min[axis], b.max[axis]);
                }
                boxes.push_back(b);
            }
            return !identifier.empty();
        }
    }

    bool VoxelShapes::ParseShape(const std::string& text, std::string& identifier, std::vector<VoxelBox>& boxes) {
        if (text.size() >= 12 && text.compare(0, 4, "\x7fMCB") == 0)
            return ParseCompiledShape(text, identifier, boxes);
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
