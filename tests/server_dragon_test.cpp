// The dragon fight end to end: the podium, the crystals on the pillars, the dragon and its boss bar,
// the kill (exit portal, egg, gateway), the respawn with four crystals, and a gateway trip out to
// the outer islands.
#include "test_util.hpp"

#include "support/LoopbackClient.hpp"

#include "network/session/Session.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/end/DragonFight.hpp"
#include "world/level/end/EndGen.hpp"

#include <cmath>
#include <filesystem>

using namespace perlite;
using namespace perlite::testclient;

int main() {
    namespace fs = std::filesystem;
    ServerConfig config;
    config.gameMode = 1;
    config.generator = "flat";
    config.loopbackOnly = false;
    config.nethernet = false;
    config.viewDistance = 6;
    config.operators = {"Alice"};
    config.dataDir = (fs::temp_directory_path() / "perlite_dragon_data").string();
    config.worldStorage = "none";
    config.seed = 12345;
    fs::create_directories(config.dataDir);
    Server server(config);
    std::string error;
    CHECK(server.Start(error));

    Client alice(server, "Alice");
    Join(alice);
    proto::SetLocalPlayerAsInitializedPacket init;
    alice.Send(init);
    alice.Settle();
    const auto send = [&](const std::string& line) {
        proto::CommandRequestPacket c;
        c.command = line;
        alice.Send(c);
        alice.Settle();
    };
    proto::PlayerActionPacket ack;
    ack.action = proto::Action::DimensionChangeAck;

    send("/execute in the_end run tp @s 0.5 90 30.5");
    alice.Settle(10);
    alice.Send(ack);
    Session* a = server.FindByName("Alice");
    CHECK(a && a->DimensionId() == kEnd);
    DragonFight& fight = server.Fight();
    for (int i = 0; i < 3000 && (!fight.Dragon() || fight.CrystalsAlive() < 10); i++)
        alice.Settle(1);
    const int py = fight.PortalY();
    std::printf("podium at y %d, dragon %llu, %d crystals\n", py, static_cast<unsigned long long>(fight.Dragon()),
                fight.CrystalsAlive());
    CHECK(py > 30);
    CHECK(fight.Dragon() != 0);
    CHECK(fight.CrystalsAlive() == 10);
    const auto name = [&](int x, int y, int z) { return BlockRegistry::Instance().Name(server.BlockAt(kEnd, x, y, z)); };
    CHECK(name(0, py, 0) == "minecraft:bedrock");
    CHECK(name(0, py + 3, 0) == "minecraft:bedrock");
    CHECK(name(3, py, 0) == "minecraft:bedrock");
    CHECK(name(1, py, 0) == "minecraft:air");
    CHECK(name(1, py + 2, 0) == "minecraft:torch");
    const auto bars = alice.Take<proto::BossEventPacket>();
    CHECK(!bars.empty() && bars[0].type == proto::BossEventPacketType::ShowBar);

    // The dragon flies its rounds: it moves, and stays over the island.
    ActorManager& mobs = server.MobsIn(kEnd);
    Mob* dragon = mobs.Find(fight.Dragon());
    CHECK(dragon != nullptr);
    if (dragon) {
        const double x0 = dragon->x, z0 = dragon->z;
        for (int i = 0; i < 100; i++) {
            alice.Settle(1);
            if (i % 25 == 0)
                if (const Mob* d = mobs.Find(fight.Dragon()))
                    std::printf("dragon %.1f %.1f %.1f phase %d node %d age %d ticks %d sitting %d\n", d->x, d->y, d->z, d->phase, d->node, d->age, d->phaseTicks, d->sitting);
        }
        dragon = mobs.Find(fight.Dragon());
        // (unless it has landed on the podium)
        CHECK(dragon && (std::hypot(dragon->x - x0, dragon->z - z0) > 5.0 || dragon->sitting ||
                         dragon->phase == dragonphase::Landing || dragon->phase == dragonphase::Charging ||
                         dragon->phase == dragonphase::Takeoff));
        CHECK(dragon && std::hypot(dragon->x, dragon->z) < 150.0);
    }

    // Hitting a crystal blows it up.
    const std::vector<uint64_t> crystals = fight.PillarCrystals();
    CHECK(crystals.size() == 10);
    if (!crystals.empty()) {
        mobs.Damage(crystals[0], 1.0f);
        alice.Settle(2);
        CHECK(fight.CrystalsAlive() == 9);
    }

    // The kill: ten seconds of dying, then the portal, the egg and the first gateway.
    if (dragon)
        mobs.Damage(dragon->id, 100000.0f);
    alice.Settle(1);
    dragon = mobs.Find(fight.Dragon());
    CHECK(dragon && dragon->phase == dragonphase::Dying);
    for (int i = 0; i < 260 && !fight.Killed(); i++)
        alice.Settle(1);
    CHECK(fight.Killed());
    CHECK(fight.Dragon() == 0);
    CHECK(name(1, py, 0) == "minecraft:end_portal");
    CHECK(name(0, py + 4, 0) == "minecraft:dragon_egg");
    const theend::GatewaySlot gate = theend::Gateways(config.seed)[0];
    alice.Settle(2);
    CHECK(name(gate.x, gate.y, gate.z) == "minecraft:end_gateway");
    CHECK(name(gate.x, gate.y + 2, gate.z) == "minecraft:bedrock");

    // Four crystals on the rim of the portal bring it back.
    if (a) {
        const int rim[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
        for (const auto& r : rim)
            CHECK(fight.UseEndCrystal(*a, r[0], py, r[1]));
        CHECK(name(1, py, 0) == "minecraft:air"); // the portal goes out
        for (int i = 0; i < 2000 && fight.Killed(); i++)
            alice.Settle(1);
        CHECK(!fight.Killed());
        CHECK(fight.Dragon() != 0);
        std::printf("after the respawn: %d crystals\n", fight.CrystalsAlive());
        CHECK(fight.CrystalsAlive() == 10);
    }

    // Through the gateway to the outer islands, about a thousand blocks out.
    send("/tp @s " + std::to_string(gate.x + 0.5) + " " + std::to_string(gate.y + 1.62) + " " + std::to_string(gate.z + 0.5));
    for (int i = 0; i < 4000 && a && std::hypot(a->X(), a->Z()) < 600; i++)
        alice.Settle(1);
    std::printf("after the gateway: %.1f %.1f %.1f\n", a ? a->X() : 0, a ? a->Y() : 0, a ? a->Z() : 0);
    CHECK(a && std::hypot(a->X(), a->Z()) > 700);
    CHECK(!alice.corrupt && alice.malformed == 0);

    server.Stop();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("server_dragon_test: ok\n");
    return 0;
}
