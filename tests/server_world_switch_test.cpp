// Worlds played one after another in one process (a client's singleplayer worlds) must not leak
// generator state into each other: a world looks the same whatever was played before it, and its
// spawn stands on the ground.
#include "test_util.hpp"

#include "support/LoopbackClient.hpp"

#include "network/session/Session.hpp"
#include "world/level/block/Blocks.hpp"

#include <filesystem>
#include <vector>

using namespace perlite;
using namespace perlite::testclient;

namespace {
    struct Look {
        int spawnY = 0;
        std::vector<uint16_t> blocks;
    };

    Look Play(const std::string& generator, int64_t seed) {
        namespace fs = std::filesystem;
        ServerConfig config;
        config.gameMode = 1;
        config.generator = generator;
        config.loopbackOnly = false;
        config.nethernet = false;
        config.viewDistance = 4;
        config.dataDir = (fs::temp_directory_path() / "perlite_switch_data").string();
        config.worldStorage = "none";
        config.seed = seed;
        config.randomSeed = false;
        fs::create_directories(config.dataDir);
        Server server(config);
        std::string error;
        CHECK(server.Start(error));
        Client c(server, "Alice");
        Join(c);
        proto::SetLocalPlayerAsInitializedPacket init;
        c.Send(init);
        const int sx = static_cast<int>(server.SpawnX()), sz = static_cast<int>(server.SpawnZ());
        for (int i = 0; i < 2000 && (!server.Chunks().Column((sx >> 4) + 1, (sz >> 4) + 1) ||
                                     !server.Chunks().Column((sx >> 4) - 1, (sz >> 4) - 1));
             i++)
            c.Settle(1);
        c.Settle(20);
        Look out;
        out.spawnY = static_cast<int>(server.SpawnY());
        for (int x = sx - 8; x <= sx + 8; x++)
            for (int z = sz - 8; z <= sz + 8; z++)
                for (int y = kMinY; y < kMinY + kHeight; y++)
                    out.blocks.push_back(server.BlockAt(kOverworld, x, y, z));
        // The spawn is free: the feet and head blocks are air and something is below.
        CHECK(server.BlockAt(kOverworld, sx, out.spawnY, sz) == BlockRegistry::kAir);
        CHECK(server.BlockAt(kOverworld, sx, out.spawnY + 1, sz) == BlockRegistry::kAir);
        server.Stop();
        return out;
    }
}

int main() {
    const int64_t seed = 6618995517318049792LL;
    const Look first = Play("vanilla", seed);
    (void)Play("flat", 99);
    (void)Play("vanilla", 4242);
    const Look again = Play("vanilla", seed);
    size_t diff = 0;
    for (size_t i = 0; i < first.blocks.size() && i < again.blocks.size(); i++)
        diff += first.blocks[i] != again.blocks[i];
    std::printf("spawn y %d then %d, %zu blocks differ\n", first.spawnY, again.spawnY, diff);
    CHECK(first.spawnY == again.spawnY);
    CHECK(diff == 0);
    if (g_failed)
        return 1;
    std::printf("server_world_switch_test: ok\n");
    return 0;
}
