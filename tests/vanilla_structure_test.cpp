// Structures from the templates of a Bedrock vanilla pack: every template block state has a Bedrock
// block, and a village found near spawn is assembled from its pieces and written into the chunks.
// Needs a pack (PERLITE_STRUCTURE_PACK, or a Bedrock Dedicated Server's behavior_packs/vanilla given
// as the second argument); skipped without one.
#include "test_util.hpp"

#include "server/Config.hpp"
#include "server/Server.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"
#include "world/level/structure/Structures.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <thread>

using namespace perlite;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "resources";
    std::string pack = argc > 2 ? argv[2] : "";
    if (const char* env = std::getenv("PERLITE_STRUCTURE_PACK"))
        pack = env;
    std::string error;
    if (!BlockCatalog::Instance().Load(BlockCatalog::FindFile(dir, "block_states.json"), BlockCatalog::FindFile(dir, "block_destroy.tsv"), error)) {
        std::printf("block data: %s\n", error.c_str());
        return 1;
    }
    const auto store = structure::TemplateStore::Find(pack, ".");
    if (pack.empty() || !store) {
        std::printf("vanilla_structure_test: no structure pack, skipped\n");
        return 0;
    }

    // Every state of every template has a Bedrock block.
    std::map<std::string, int> missing;
    int states = 0, templates = 0;
    for (const auto& e : fs::recursive_directory_iterator(store->Directory())) {
        if (e.path().extension() != ".nbt")
            continue;
        const std::string name = fs::relative(e.path(), store->Directory()).replace_extension().generic_string();
        const auto t = store->Get(name);
        CHECK(t != nullptr);
        if (!t)
            continue;
        templates++;
        for (int turns = 0; turns < 4; turns++) {
            const auto& palette = t->Bedrock(turns);
            for (size_t i = 0; i < palette.size(); i++) {
                states++;
                const std::string bare = structure::StripNamespace(t->palette[i].name);
                if (palette[i] == BlockRegistry::kAir && bare != "air" && bare != "cave_air" && bare != "structure_void" && bare != "jigsaw")
                    missing[t->palette[i].name + " -> " + structure::BedrockText(t->palette[i])]++;
            }
        }
    }
    std::printf("%d templates, %d states, %zu without a Bedrock block\n", templates, states, missing.size());
    for (const auto& [name, n] : missing)
        std::printf("  missing %s (%d)\n", name.c_str(), n);
    CHECK(missing.empty());

    // A village near the world's spawn.
    ServerConfig config;
    config.seed = 12345;
    config.structurePack = pack;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    CHECK(backend && backend->Structures());
    if (!backend || !backend->Structures())
        return 1;
    structure::StructureGenerator& gen = *backend->Structures();
    int vx = 0, vz = 0;
    const auto began = std::chrono::steady_clock::now();
    const bool found = gen.Locate("village", 0, 0, 100, vx, vz);
    std::printf("village at %d %d (%s), %.0f ms\n", vx, vz, found ? "found" : "none",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
    CHECK(found);
    if (!found)
        return 1;
    const auto near = gen.StartsNear(vx >> 4, vz >> 4);
    size_t pieces = 0;
    std::string type;
    for (const auto& s : near)
        if (s->type.starts_with("village")) {
            pieces = s->pieces.size();
            type = s->type;
            std::set<std::string> names;
            for (const auto& p : s->pieces)
                names.insert(p.tmpl->name.substr(0, p.tmpl->name.rfind('/')));
            for (const std::string& n : names)
                std::printf("  pool %s\n", n.c_str());
        }
    std::printf("%s with %zu pieces\n", type.c_str(), pieces);
    CHECK(pieces >= 10);

    ChunkManager chunks(std::move(backend));
    const int cx = vx >> 4, cz = vz >> 4;
    for (;;) {
        int missingChunks = 0;
        for (int x = cx - 3; x <= cx + 3; x++)
            for (int z = cz - 3; z <= cz + 3; z++)
                missingChunks += chunks.Get({x, z}) == nullptr;
        chunks.Pump();
        if (!missingChunks)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::map<std::string, int> counts;
    for (int x = cx * 16 - 48; x < cx * 16 + 64; x++)
        for (int z = cz * 16 - 48; z < cz * 16 + 64; z++)
            for (int y = 40; y < 140; y++) {
                const std::string n = BlockRegistry::Instance().Name(chunks.GetBlock(x, y, z));
                if (n.contains("planks") || n.contains("bell") || n.contains("grass_path") || n.contains("door") || n.contains("bed") ||
                    n.contains("sandstone") || n.contains("terracotta"))
                    counts[n]++;
            }
    for (const auto& [n, c] : counts)
        std::printf("  %s %d\n", n.c_str(), c);
    CHECK(counts["minecraft:grass_path"] + counts["minecraft:sandstone"] + counts["minecraft:smooth_sandstone"] > 20);
    CHECK(counts["minecraft:wooden_door"] + counts["minecraft:spruce_door"] + counts["minecraft:acacia_door"] + counts["minecraft:jungle_door"] > 0);

    // The structures built in code.
    for (const char* type : {"stronghold", "desert_pyramid", "jungle_pyramid", "swamp_hut", "mineshaft", "monument", "mansion",
                             "buried_treasure", "igloo", "shipwreck", "ruined_portal", "trail_ruins", "ancient_city", "pillager_outpost"}) {
        int sx = 0, sz = 0;
        const auto t0 = std::chrono::steady_clock::now();
        const std::string_view tv(type);
        const int radius = tv == "mansion" ? 800 : tv == "desert_pyramid" || tv == "swamp_hut" ? 400 : tv == "buried_treasure" || tv == "mineshaft" ? 60 : 160;
        const bool ok = gen.Locate(type, 0, 0, radius, sx, sz);
        size_t n = 0;
        if (ok)
            for (const auto& st : gen.StartsNear(sx >> 4, sz >> 4))
                if (st->type == type)
                    n = st->pieces.size();
        std::printf("  %s: %s at %d %d, %zu pieces (%.0f ms)\n", type, ok ? "found" : "none", sx, sz, n,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        CHECK(ok && n > 0);
    }
    {
        int sx = 0, sz = 0;
        CHECK(gen.Locate("stronghold", 0, 0, 200, sx, sz));
        int frames = 0;
        for (const auto& st : gen.StartsNear(sx >> 4, sz >> 4))
            if (st->type == "stronghold")
                for (const auto& p : st->pieces)
                    if (p.tmpl->name.contains("portal"))
                        for (const auto& s2 : p.tmpl->palette)
                            frames += s2.name.contains("end_portal_frame");
        std::printf("  stronghold portal room frame states: %d\n", frames);
        CHECK(frames > 0);
    }

    // Through a server: the village's chunks come with their villagers.
    {
        ServerConfig sc;
        sc.seed = 12345;
        sc.structurePack = pack;
        sc.worldStorage = "none";
        sc.nethernet = false;
        sc.dataDir = (fs::temp_directory_path() / "perlite_structure_data").string();
        fs::create_directories(sc.dataDir);
        Server server(sc);
        std::string startError;
        CHECK(server.Start(startError));
        for (int x = cx - 4; x <= cx + 4; x++)
            for (int z = cz - 4; z <= cz + 4; z++)
                server.World(kOverworld).Request({x, z});
        int villagers = 0;
        for (int i = 0; i < 3000 && villagers == 0; i++) {
            server.Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            villagers = 0;
            for (const Mob& m : server.MobsIn(kOverworld).All())
                villagers += std::string_view(m.def->id).contains("villager");
        }
        std::printf("%d villagers spawned with the village\n", villagers);
        CHECK(villagers > 0);
        server.Stop();
    }

    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("vanilla_structure_test: ok\n");
    return 0;
}
