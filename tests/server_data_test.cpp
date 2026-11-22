// Checks the default server.properties Perlite ships with, and the readers for the vanilla behavior
// pack (which is downloaded at runtime, never shipped). The checks against the real pack run only
// when PERLITE_VANILLA_PACK points at a downloaded copy.
#include "test_util.hpp"

#include "common/FileIo.hpp"
#include "server/Config.hpp"
#include "world/actor/mob/VanillaEntities.hpp"
#include "world/level/block/VoxelShapes.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

using namespace perlite;

static bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

static void TestShippedProperties(const std::string& root) {
    std::string shipped;
    CHECK(file::Read(root + "/server.properties", shipped));
    // The file in the repository is exactly what a first start writes.
    CHECK(shipped == ServerConfig::FirstRun().PropertiesText());
    std::istringstream lines(shipped);
    std::string line, previous;
    int settings = 0;
    while (std::getline(lines, line)) {
        CHECK(!line.empty());
        if (line.find('=') != std::string::npos && line[0] != '#') {
            settings++;
            CHECK(previous.starts_with("#"));
        }
        previous = line;
    }
    CHECK(settings > 20);
}

static void TestEntityParser() {
    // A made-up entity in the pack's format: adult and baby scale groups and a size class.
    const char* text = R"({"format_version": "1.21.0", "minecraft:entity": {
        "description": {"identifier": "test:hopper"},
        // comments are allowed in pack files
        "component_groups": {
            "test:adult": {"minecraft:scale": {"value": 0.6}},
            "test:baby": {"minecraft:is_baby": {}, "minecraft:scale": {"value": 0.4}},
            "test:size_large": {"minecraft:variant": {"value": 4},
                                "minecraft:collision_box": {"width": 2.0, "height": 2.0},
                                "minecraft:health": {"value": 16, "max": 16}}
        },
        "components": {"minecraft:collision_box": {"width": 0.4, "height": 0.5},
                       "minecraft:health": {"value": 3, "max": 3},
                       "minecraft:movement": {"value": 0.3}}}})";
    VanillaEntity e;
    CHECK(VanillaEntities::ParseEntity(text, e));
    CHECK(e.identifier == "test:hopper");
    CHECK(Near(e.width, 0.4f) && Near(e.height, 0.5f) && Near(e.health, 3) && Near(e.speed, 0.3f));
    CHECK(Near(e.scale, 0.6f) && Near(e.babyScale, 0.4f));
    CHECK(e.sizes.size() == 1 && e.sizes[0].variant == 4 && Near(e.sizes[0].width, 2.0f));
}

static void TestVanillaEntities(const std::string& pack) {
    VanillaEntities& v = VanillaEntities::Instance();
    CHECK(v.Load(pack) > 100);
    const VanillaEntity* rabbit = v.Get("minecraft:rabbit");
    CHECK(rabbit != nullptr);
    if (rabbit) {
        CHECK(Near(rabbit->scale, 0.6f));
        CHECK(Near(rabbit->babyScale, 0.4f));
        CHECK(!rabbit->variants.empty());
    }
    const VanillaEntity* slime = v.Get("minecraft:slime");
    CHECK(slime != nullptr);
    if (slime) {
        CHECK(slime->sizes.size() == 3);
        bool small = false, medium = false, large = false;
        for (const VanillaSize& s : slime->sizes) {
            small |= s.variant == 1;
            medium |= s.variant == 2;
            large |= s.variant == 4;
        }
        CHECK(small && medium && large);
    }
}

static void TestVoxelShapes() {
    // A full block is one solid cell.
    const proto::VoxelShape cube = VoxelShapes::ToCells({VoxelBox{{0, 0, 0}, {1, 1, 1}}});
    CHECK(cube.cells.xSize == 1 && cube.cells.ySize == 1 && cube.cells.zSize == 1);
    CHECK(cube.cells.storage.size() == 1 && cube.cells.storage[0] == 1);
    CHECK(cube.xCoordinates.size() == 2 && cube.xCoordinates[1] == 1.0f);

    // A stair: the bottom half, plus the top half over the back (z >= 0.5). Two cells per axis, and
    // the cell bit is z + y*nz + x*nz*ny.
    const proto::VoxelShape stair = VoxelShapes::ToCells(
        {VoxelBox{{0, 0, 0}, {1, 0.5f, 1}}, VoxelBox{{0, 0.5f, 0.5f}, {1, 1, 1}}});
    CHECK(stair.cells.xSize == 1 && stair.cells.ySize == 2 && stair.cells.zSize == 2);
    // cells: (y0,z0)=bit0 solid, (y0,z1)=bit1 solid, (y1,z0)=bit2 empty, (y1,z1)=bit3 solid
    CHECK(stair.cells.storage.size() == 1 && stair.cells.storage[0] == 0b1011);

    // No boxes: an empty shape with a single coordinate per axis.
    const proto::VoxelShape empty = VoxelShapes::ToCells({});
    CHECK(empty.cells.storage.empty() && empty.xCoordinates.size() == 1);

    std::string id;
    std::vector<VoxelBox> boxes;
    CHECK(VoxelShapes::ParseShape(R"({"minecraft:voxel_shape":{"description":{"identifier":"minecraft:t"},
        "shape":{"boxes":[{"min":[0,8,0],"max":[16,16,16]}]}}})", id, boxes));
    CHECK(id == "minecraft:t" && boxes.size() == 1 && Near(boxes[0].min[1], 0.5f) && Near(boxes[0].max[0], 1.0f));
}

static void TestVanillaShapes(const std::string& pack) {
    VoxelShapes& shapes = VoxelShapes::Instance();
    CHECK(shapes.Load(pack) > 200);
    const proto::VoxelShapesPacket& packet = shapes.Packet();
    CHECK(packet.shapes.size() == packet.nameMap.size());
    bool slab = false;
    for (const auto& entry : packet.nameMap) {
        CHECK(entry.id < packet.shapes.size());
        if (entry.name == "minecraft:slab_type_top") {
            slab = true;
            const proto::VoxelShape& s = packet.shapes[entry.id];
            CHECK(s.yCoordinates.size() == 2 && Near(s.yCoordinates[0], 0.5f));
        }
    }
    CHECK(slab);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: server_data_test <source dir>\n");
        return 2;
    }
    const std::string root = argv[1];
    TestShippedProperties(root);
    TestEntityParser();
    TestVoxelShapes();
    const char* pack = std::getenv("PERLITE_VANILLA_PACK");
    if (pack && *pack) {
        TestVanillaEntities(pack);
        TestVanillaShapes(pack);
    } else {
        std::printf("server_data_test: PERLITE_VANILLA_PACK not set, skipping the checks against the real pack\n");
    }
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("server_data_test: ok\n");
    return 0;
}
