// Checks the default server.properties Perlite ships with, and the readers for the vanilla behavior
// pack (which is downloaded at runtime, never shipped). The checks against the real pack run only
// when PERLITE_VANILLA_PACK points at a downloaded copy.
#include "test_util.hpp"
#include <filesystem>

#include "common/FileIo.hpp"
#include "server/Config.hpp"
#include "server/VanillaPack.hpp"
#include <miniz.h>
#include "world/actor/mob/VanillaEntities.hpp"
#include "world/level/block/VoxelShapes.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/chunk/Chunk.hpp"
#include "world/level/block/Waterlog.hpp"
#include "world/level/storage/WorldStorage.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/levelgen/Generator.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

#include <random>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlite;

static bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

static void TestShippedProperties(const std::string& root) {
    std::string shipped;
    CHECK(file::Read(root + "/server.properties", shipped));
    // A Windows checkout may turn the line endings into CRLF; the server reads either.
    std::erase(shipped, '\r');
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

// The bytes sent for a chunk column must not change when the encoder is reworked: columns with
// uniform sections, few and many block kinds, and both biome forms, hashed against the value the
// original encoder produced.
static constexpr uint64_t kChunkEncodingHash = 0x04780ce9134e444dull;

static uint64_t Fnv(const std::vector<uint8_t>& bytes, uint64_t h = 1469598103934665603ull) {
    for (const uint8_t b : bytes)
        h = (h ^ b) * 1099511628211ull;
    return h;
}

static void TestChunkEncoding() {
    BlockCatalog& catalog = BlockCatalog::Instance();
    catalog.LoadBuiltin();
    std::vector<uint16_t> kinds;
    for (const char* n : {"minecraft:stone", "minecraft:dirt", "minecraft:grass_block", "minecraft:water",
                          "minecraft:oak_log", "minecraft:deepslate", "minecraft:sand", "minecraft:gravel"})
        kinds.push_back(catalog.DefaultBlock(n));
    std::vector<uint16_t> many;
    for (uint16_t id = 1; many.size() < 300; id++)
        many.push_back(id);
    std::mt19937 rng(7);
    uint64_t h = 1469598103934665603ull;
    for (int variant = 0; variant < 4; variant++) {
        ChunkColumn col;
        for (int sec = 0; sec < kSections; sec++) {
            const int mode = (sec + variant) % 5;
            if (mode == 0)
                continue;  // air
            if (mode == 1) {
                col.FillSection(sec, kinds[static_cast<size_t>(sec) % kinds.size()]);
                continue;
            }
            for (int i = 0; i < kCells; i++) {
                const int x = i >> 8, z = (i >> 4) & 15, y = i & 15;
                uint16_t b;
                if (mode == 2)
                    b = kinds[rng() % 2];
                else if (mode == 3)
                    b = kinds[rng() % kinds.size()];
                else
                    b = many[rng() % many.size()];
                col.Set(x, kMinY + sec * 16 + y, z, b);
            }
        }
        if (variant % 2 == 0) {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    col.SetBiome(x, z, static_cast<uint8_t>((x + z) % 4), static_cast<uint8_t>((x * z) % 7));
        } else {
            std::vector<uint8_t> table(static_cast<size_t>(16 * ChunkColumn::kQuartLayers)),
                network(table.size());
            for (size_t i = 0; i < table.size(); i++) {
                table[i] = static_cast<uint8_t>(i % 5);
                network[i] = static_cast<uint8_t>((i / 7) % 9);
            }
            col.SetQuartBiomes(table.data(), network.data());
        }
        for (int dim : {kOverworld, kNether, kEnd}) {
            std::vector<uint8_t> out;
            col.EncodeNetwork(out, dim);
            h = Fnv(out, h);
        }
    }
    std::printf("server_data_test: chunk encoding hash %016llx\n", static_cast<unsigned long long>(h));
    const char* expected = std::getenv("PERLITE_EXPECT_CHUNK_HASH");
    CHECK(h == (expected ? std::strtoull(expected, nullptr, 16) : kChunkEncodingHash));
}

// The generation queue serves the chunk nearest a player first and drops chunks no player can reach.
namespace {
    class RecordingBackend : public TerrainBackend {
    public:
        const char* Name() const override { return "recording"; }
        bool ThreadSafe() const override { return true; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn>) override {
            std::unique_lock lock(mutex);
            for (const ChunkPos& p : positions)
                order.push_back(p);
            if (!open)
                gate.wait(lock, [&] { return open; });
        }
        void Open() {
            {
                std::lock_guard lock(mutex);
                open = true;
            }
            gate.notify_all();
        }
        std::mutex mutex;
        std::condition_variable gate;
        bool open = false;
        std::vector<ChunkPos> order;
    };
}

static void TestChunkQueueOrder() {
    auto backend = std::make_unique<RecordingBackend>();
    RecordingBackend* rec = backend.get();
    ChunkManager world(std::move(backend), 1);
    world.Get({1000, 1000});  // taken first and held, so the rest queue up behind it
    for (int i = 0; i < 200 && [&] { std::lock_guard l(rec->mutex); return rec->order.empty(); }(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::vector<ChunkPos> asked;
    for (int x = -6; x <= 6; x++)
        for (int z = -6; z <= 6; z++)
            asked.push_back({x, z});
    asked.push_back({40, 40});
    asked.push_back({-40, 3});
    std::mt19937 shuffle(3);
    std::shuffle(asked.begin(), asked.end(), shuffle);
    for (const ChunkPos& p : asked)
        world.Get(p);
    world.SetViewers({{{0, 0}, 3}});
    rec->Open();
    for (int i = 0; i < 400 && world.Pending() > 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        world.Pump();
    }
    std::vector<ChunkPos> order;
    {
        std::lock_guard lock(rec->mutex);
        order.assign(rec->order.begin() + 1, rec->order.end());
    }
    // Only chunks within reach (radius 3 plus a margin of 2) were made, nearest first.
    int last = -1;
    bool sorted = true, inReach = true;
    for (const ChunkPos& p : order) {
        const int d = p.x * p.x + p.z * p.z;
        sorted &= d >= last;
        inReach &= d <= 25;
        last = d;
    }
    CHECK(!order.empty() && sorted && inReach);
    CHECK(std::ranges::find(order, ChunkPos{40, 40}) == order.end());
    // A dropped chunk can be asked for again.
    world.SetViewers({{{40, 40}, 2}});
    CHECK(world.Get({40, 40}) == nullptr);
    for (int i = 0; i < 400 && !world.Peek({40, 40}); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        world.Pump();
    }
    CHECK(world.Peek({40, 40}) != nullptr);
}


// Kelp and seagrass always get water in the second layer; a fence only when water touches it; the
// layer goes into the network chunk and through the disk format.
static void TestWaterlogging() {
    BlockCatalog& catalog = BlockCatalog::Instance();
    catalog.LoadBuiltin();
    const uint16_t water = catalog.DefaultBlock("minecraft:water");
    const uint16_t seagrass = catalog.DefaultBlock("minecraft:seagrass");
    const uint16_t fence = catalog.DefaultBlock("minecraft:oak_fence");
    const uint16_t stone = catalog.DefaultBlock("minecraft:stone");
    ChunkColumn c;
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            c.Set(x, 0, z, stone);
    c.Set(3, 1, 3, seagrass);
    c.Set(8, 1, 8, fence);
    c.Set(9, 1, 8, water);
    c.Set(12, 1, 12, fence);
    waterlog::FillGenerated(c);
    CHECK(c.GetLiquid(3, 1, 3) == water);
    CHECK(c.GetLiquid(8, 1, 8) == water);
    CHECK(c.GetLiquid(12, 1, 12) == 0);
    std::vector<uint8_t> net;
    c.EncodeNetwork(net);
    const int section = (1 - kMinY) >> 4;
    std::string disk;
    WorldStorage::EncodeSubChunk(c, section, section + (kMinY >> 4), disk);
    std::vector<uint16_t> cells(kCells), liquid(kCells);
    uint16_t uniform = 0;
    bool isUniform = false, hasLiquid = false;
    CHECK(WorldStorage::DecodeSubChunk(disk, cells.data(), uniform, isUniform, liquid.data(), &hasLiquid));
    CHECK(hasLiquid && liquid[static_cast<size_t>(CellIndex(3, 1 & 15, 3))] == water);
    CHECK(disk.size() > 3 && disk[1] == 2);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: server_data_test <source dir>\n");
        return 2;
    }
    const std::string root = argv[1];
    TestShippedProperties(root);
    TestEntityParser();
    TestChunkEncoding();
    TestChunkQueueOrder();
    TestVoxelShapes();
    TestWaterlogging();
    const char* pack = std::getenv("PERLITE_VANILLA_PACK");
    if (pack && *pack) {
        TestVanillaEntities(pack);
        TestVanillaShapes(pack);
    } else {
        std::printf("server_data_test: PERLITE_VANILLA_PACK not set, skipping the checks against the real pack\n");
    }
    // Only the vanilla behavior packs of a server download are kept, merged in version order with
    // their packed folders unpacked.
    {
        // A .brarchive with two entries.
        const auto archive = [](const std::vector<std::pair<std::string, std::string>>& files) {
            std::string out(16 + files.size() * 256, '\0');
            const uint64_t magic = 0x267052A0B125277DULL;
            std::memcpy(out.data(), &magic, 8);
            const uint32_t n = static_cast<uint32_t>(files.size()), version = 1;
            std::memcpy(out.data() + 8, &n, 4);
            std::memcpy(out.data() + 12, &version, 4);
            uint32_t offset = 0;
            std::string body;
            for (size_t i = 0; i < files.size(); i++) {
                char* e = out.data() + 16 + i * 256;
                e[0] = static_cast<char>(files[i].first.size());
                std::memcpy(e + 1, files[i].first.data(), files[i].first.size());
                const uint32_t size = static_cast<uint32_t>(files[i].second.size());
                std::memcpy(e + 248, &offset, 4);
                std::memcpy(e + 252, &size, 4);
                offset += size;
                body += files[i].second;
            }
            return out + body;
        };
        mz_zip_archive zip{};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        const auto add = [&](const std::string& name, const std::string& body) {
            mz_zip_writer_add_mem(&zip, name.c_str(), body.data(), body.size(), MZ_DEFAULT_COMPRESSION);
        };
        add("bedrock_server", "x");
        add("behavior_packs/vanilla/structures/village/plains/a.nbt", "x");
        add("behavior_packs/vanilla/loot_tables/chests/b.json", "{}");
        add("behavior_packs/vanilla/__brarchive/entities.brarchive", archive({{"pig.json", "old"}, {"cow.json", "cow"}}));
        add("behavior_packs/vanilla/__brarchive/shapes.brarchive", archive({{"anvil.json", "{}"}}));
        add("behavior_packs/vanilla_1.21.0/__brarchive/entities.brarchive", archive({{"pig.json", "new"}}));
        add("behavior_packs/vanilla_1.21.0/structures/trial_chambers/c.nbt", "x");
        add("behavior_packs/editor/__brarchive/entities.brarchive", archive({{"editor.json", "no"}}));
        add("resource_packs/vanilla/textures/x.png", "no");
        void* data = nullptr;
        size_t size = 0;
        CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string zipPath = (tmp / "perlite_bds_test.zip").string();
        {
            std::ofstream f(zipPath, std::ios::binary);
            f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        mz_free(data);
        mz_zip_writer_end(&zip);
        const std::string target = (tmp / "perlite_bds_extract").string();
        size_t kept = 0;
        std::string err;
        CHECK(perlite::vanillapack::ExtractPack(zipPath, target, kept, err));
        std::string text;
        CHECK(perlite::file::Read(target + "/entities/pig.json", text) && text == "new");
        CHECK(perlite::file::Read(target + "/entities/cow.json", text) && text == "cow");
        CHECK(std::filesystem::exists(target + "/shapes/anvil.json"));
        CHECK(std::filesystem::exists(target + "/structures/village/plains/a.nbt"));
        CHECK(std::filesystem::exists(target + "/structures/trial_chambers/c.nbt"));
        CHECK(!std::filesystem::exists(target + "/entities/editor.json"));
        CHECK(!std::filesystem::exists(target + "/bedrock_server") && !std::filesystem::exists(target + "/textures"));
        CHECK(perlite::vanillapack::HasStructures(target));
        std::filesystem::remove_all(target);
        std::filesystem::remove(zipPath);
    }

    // A real server download, when one is given: the kept pack must have what the server reads.
    if (const char* bds = std::getenv("PERLITE_BDS_ZIP"); bds && *bds) {
        const std::string target = (std::filesystem::temp_directory_path() / "perlite_bds_real").string();
        size_t kept = 0;
        std::string err;
        CHECK(perlite::vanillapack::ExtractPack(bds, target, kept, err));
        std::printf("kept %zu files from %s%s\n", kept, bds, err.empty() ? "" : (" (" + err + ")").c_str());
        CHECK(perlite::vanillapack::HasStructures(target));
        CHECK(std::filesystem::exists(target + "/structures/trial_chambers"));
        CHECK(perlite::VanillaEntities::Instance().Load(target) > 100);
        CHECK(perlite::VoxelShapes::Instance().Load(target) > 100);
        std::printf("entities %zu\n", perlite::VanillaEntities::Instance().Load(target));
        // The compiled shape reads the same as the JSON one of the same block.
        std::string compiled, id;
        std::vector<perlite::VoxelBox> boxes;
        CHECK(perlite::file::Read(target + "/shapes/fence.json", compiled) &&
              perlite::VoxelShapes::ParseShape(compiled, id, boxes) && id == "minecraft:fence" && boxes.size() == 1 &&
              std::abs(boxes[0].min[0] - 0.375f) < 1e-6f && std::abs(boxes[0].max[1] - 1.0f) < 1e-6f);
        if (const char* keep = std::getenv("PERLITE_BDS_KEEP"); keep && *keep) {
            std::filesystem::remove_all(keep);
            std::filesystem::rename(target, keep);
        }
        std::filesystem::remove_all(target);
    }

    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("server_data_test: ok\n");
    return 0;
}
