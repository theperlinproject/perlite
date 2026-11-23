// The nether's and the End's generated features: the dragon fight's pillars stand where the seed puts
// them, and the nether has its ores and glowstone.
#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/end/EndGen.hpp"
#include "world/level/levelgen/vanilla/VanillaDimension.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>

using namespace perlite;

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

namespace {
    void Load(ChunkManager& chunks, int x0, int z0, int x1, int z1) {
        for (;;) {
            int missing = 0;
            for (int x = x0; x <= x1; x++)
                for (int z = z0; z <= z1; z++)
                    missing += chunks.Get({x, z}) == nullptr;
            chunks.Pump();
            if (!missing)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "resources";
    std::string error;
    if (!BlockCatalog::Instance().Load(BlockCatalog::FindFile(dir, "block_states.json"), BlockCatalog::FindFile(dir, "block_destroy.tsv"), error)) {
        std::printf("block data: %s\n", error.c_str());
        return 1;
    }
    ServerConfig config;
    config.seed = 12345;
    BlockRegistry& reg = BlockRegistry::Instance();
    {
        auto backend = vanilla::VanillaDimensionBackend::Create(config, kEnd, error);
        CHECK(backend);
        ChunkManager chunks(std::move(backend), 0, kEnd);
        Load(chunks, -4, -4, 3, 3);
        const auto spikes = theend::Spikes(config.seed);
        CHECK(spikes.size() == 10);
        int guarded = 0;
        for (const auto& s : spikes) {
            const int cx = s.x >> 4, cz = s.z >> 4;
            Load(chunks, cx - 1, cz - 1, cx + 1, cz + 1);
            CHECK(reg.Name(chunks.GetBlock(s.x, s.height - 1, s.z)) == "minecraft:obsidian");
            CHECK(reg.Name(chunks.GetBlock(s.x, s.height, s.z)) == "minecraft:bedrock");
            CHECK(reg.Name(chunks.GetBlock(s.x + s.radius, s.height - 2, s.z)) == "minecraft:obsidian");
            guarded += s.guarded;
            if (s.guarded)
                CHECK(reg.Name(chunks.GetBlock(s.x + 2, s.height + 1, s.z)) == "minecraft:iron_bars");
        }
        CHECK(guarded == 2);
        std::printf("end: 10 pillars, %d caged\n", guarded);
    }
    {
        auto backend = vanilla::VanillaDimensionBackend::Create(config, kNether, error);
        CHECK(backend);
        ChunkManager chunks(std::move(backend), 0, kNether);
        Load(chunks, -3, -3, 2, 2);
        std::map<std::string, int> count;
        for (int x = -32; x < 32; x++)
            for (int z = -32; z < 32; z++)
                for (int y = 1; y < 127; y++)
                    count[reg.Name(chunks.GetBlock(x, y, z))]++;
        for (const char* name : {"minecraft:quartz_ore", "minecraft:nether_gold_ore", "minecraft:glowstone", "minecraft:magma"}) {
            std::printf("nether: %s %d\n", name, count[name]);
            CHECK(count[name] > 0);
        }
    }
    std::printf("vanilla_dimension_test: ok\n");
    return 0;
}
