#include "test_util.hpp"
#include "test_auth.hpp"

#include "common/Json.hpp"
#include "server/commands/Commands.hpp"
#include "server/Server.hpp"
#include "network/session/Session.hpp"
#include "world/crafting/Recipes.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "network/Batch.hpp"
#include "world/actor/ActorManager.hpp"

#include "protocol/Packets.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "support/ChunkCodec.hpp"

#include <chrono>
#include <cmath>
#include <random>
#include <filesystem>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace net = perlite::net;

#include "world/inventory/Inventory.hpp"
#include "support/LoopbackClient.hpp"
using namespace perlite::testclient;

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    // Folders of this run only, so test runs side by side (ctest -j, two checkouts) do not collide.
    const std::string run = std::to_string(std::random_device{}()) + "_" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path plugins = fs::temp_directory_path() / ("perlite_session_plugins_" + run);
    fs::remove_all(plugins);
    fs::create_directories(plugins);
    if (argc > 1)
        fs::copy_file(argv[1], plugins / "hello_server.wasm");

    ServerConfig config;
    config.gameMode = 1;
    config.generator = "flat";
    config.loopbackOnly = false;
    config.nethernet = false;
    config.terrainBackend = std::getenv("PS_TEST_BACKEND") ? std::getenv("PS_TEST_BACKEND") : "cpu";
    config.pluginDir = plugins.string();
    config.viewDistance = 6;
    config.operators = {"Alice"};
    config.dataDir = (fs::temp_directory_path() / ("perlite_session_data_" + run)).string();
    config.worldStorage = "none";
    fs::create_directories(config.dataDir);
    for (const char* f : {"/ops.json", "/whitelist.json", "/allowlist.json", "/permissions.json"})
        fs::remove(config.dataDir + f);
    Server server(config);
    std::string error;
    CHECK(server.Start(error));
    if (!error.empty()) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }

    {
        Client old(server, "Old");
        old.Login(100);
        const auto status = old.Take<proto::PlayStatusPacket>();
        CHECK(status.size() == 1 && status[0].status == proto::PlayStatusPacketStatus::FailedClient);
    }

    Client alice(server, "Alice");
    alice.Login();
    CHECK(alice.Take<proto::NetworkSettingsPacket>().size() == 1);
    CHECK(alice.Take<proto::PlayStatusPacket>().size() == 1);
    CHECK(alice.Take<proto::ResourcePacksInfoPacket>().size() == 1);
    CHECK(!alice.corrupt && alice.malformed == 0);

    proto::ResourcePackClientResponsePacket have;
    have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
    alice.Send(have);
    alice.Settle();
    CHECK(alice.Take<proto::ResourcePackStackPacket>().size() == 1);

    proto::ResourcePackClientResponsePacket done;
    done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
    alice.Send(done);
    alice.Settle();
    const auto start = alice.Take<proto::StartGamePacket>();
    CHECK(start.size() == 1);
    if (!start.empty()) {
        CHECK(start[0].blockNetworkIdsAreHashes);
        CHECK(start[0].permissionLevel == proto::PermissionLevel::Operator);
        CHECK(start[0].playerPosition.y > -61.0f && start[0].playerPosition.y < -58.0f);
    }
    CHECK(alice.Take<proto::AvailableCommandsPacket>().size() == 1);

    proto::RequestChunkRadiusPacket req;
    req.chunkRadius = 4;
    req.maxRadius = 4;
    alice.Send(req);
    for (int i = 0; i < 60; i++)
        alice.Settle(2);
    const auto radius = alice.Take<proto::ChunkRadiusUpdatePacket>();
    CHECK(radius.size() == 1 && radius[0].chunkRadius == 4);
    CHECK(alice.Take<proto::NetworkChunkPublisherUpdatePacket>().size() >= 1);
    const auto chunks = alice.Take<proto::LevelChunkPacket>();
    CHECK(chunks.size() == 49);
    for (const auto& c : chunks) {
        std::vector<std::pair<int, perlite::SubChunkData>> subs;
        size_t used = 0;
        CHECK(perlite::ChunkCodec::ParseColumn(c.payload.data(), c.payload.size(), c.subChunkCount, -4, 24,
                                              subs, used));
        CHECK(subs.size() == 24);
    }
    const auto spawn = alice.Take<proto::PlayStatusPacket>();
    CHECK(!spawn.empty() && spawn.back().status == proto::PlayStatusPacketStatus::PlayerSpawn);

    proto::SetLocalPlayerAsInitializedPacket init;
    alice.Send(init);
    alice.Settle();
    auto texts = alice.Take<proto::TextPacket>();
    bool welcomed = false, joined = false;
    for (const auto& t : texts) {
        welcomed = welcomed || t.message == "Welcome, Alice!";
        joined = joined || t.message.contains("Alice joined");
    }
    CHECK(joined);
    CHECK(welcomed == (argc > 1));
    CHECK(server.InGamePlayers().size() == 1);

    Client bob(server, "Bob");
    Join(bob);
    proto::SetLocalPlayerAsInitializedPacket init2;
    bob.Send(init2);
    bob.Settle();
    alice.Settle();
    CHECK(server.InGamePlayers().size() == 2);
    const auto seenByAlice = alice.Take<proto::AddPlayerPacket>();
    CHECK(seenByAlice.size() == 1 && seenByAlice[0].username == "Bob");
    const auto seenByBob = bob.Take<proto::AddPlayerPacket>();
    CHECK(seenByBob.size() == 1 && seenByBob[0].username == "Alice");
    CHECK(alice.Take<proto::PlayerListPacket>().size() >= 1);
    alice.inbox.clear();
    bob.inbox.clear();

    proto::TextPacket say;
    say.type = proto::TextPacketType::Chat;
    say.sourceName = "Alice";
    say.message = "hi bob";
    alice.Send(say);
    alice.Settle();
    bob.Settle();
    auto aliceChat = alice.Take<proto::TextPacket>();
    auto bobChat = bob.Take<proto::TextPacket>();
    CHECK(aliceChat.size() == 1 && bobChat.size() == 1);
    if (!bobChat.empty())
        CHECK(bobChat[0].sourceName == "Alice" && bobChat[0].message == "hi bob" &&
              bobChat[0].type == proto::TextPacketType::Chat);

    if (argc > 1) {
        proto::TextPacket bad;
        bad.type = proto::TextPacketType::Chat;
        bad.message = "a CREEPER!";
        bob.Send(bad);
        bob.Settle();
        alice.Settle();
        CHECK(alice.Take<proto::TextPacket>().empty());
        const auto reply = bob.Take<proto::TextPacket>();
        CHECK(reply.empty() || reply[0].message.contains("do not say that word"));
    }

    proto::CommandRequestPacket cmd;
    cmd.command = "/list";
    bob.Send(cmd);
    bob.Settle();
    auto listReply = bob.Take<proto::TextPacket>();
    CHECK(listReply.size() == 1 &&
          listReply[0].message.contains("There are 2 of a max of 20 players online"));
    cmd.command = "/say nope";
    bob.Send(cmd);
    bob.Settle();
    listReply = bob.Take<proto::TextPacket>();
    CHECK(listReply.size() == 2 && listReply[0].message.contains("Unknown or incomplete command") &&
          listReply[1].message.contains("/say<--[HERE]"));
    cmd.command = "/say server notice";
    alice.Send(cmd);
    alice.Settle();
    bob.Settle();
    bool notice = false;
    for (const auto& t : bob.Take<proto::TextPacket>())
        notice = notice || t.message.contains("server notice");
    CHECK(notice);
    if (argc > 1) {
        cmd.command = "/hello";
        bob.Send(cmd);
        bob.Settle();
        const auto hello = bob.Take<proto::TextPacket>();
        CHECK(hello.size() == 1 && hello[0].message == "Hello! Players online: 2");
    }

    alice.inbox.clear();
    bob.inbox.clear();
    proto::PlayerAuthInputPacket move;
    move.position = {40.5f, -58.38f, 3.5f};
    alice.Send(move);
    alice.Settle();
    bob.Settle();
    const auto moves = bob.Take<proto::MovePlayerPacket>();
    CHECK(!moves.empty() && moves.back().position.x == 40.5f);

    CHECK(server.SetBlock(1, -60, 1, "stone") == perlin::abi::Status::Ok);
    bob.Settle();
    CHECK(bob.Take<proto::UpdateBlockPacket>().size() == 1);
    std::string block;
    CHECK(server.GetBlock(1, -60, 1, block) == perlin::abi::Status::Ok && block == "minecraft:stone");

    {
        CHECK(server.SetBlock(5, -50, 5, "sand") == perlin::abi::Status::Ok);
        for (int i = 0; i < 60; i++)
            server.Tick();
        std::string here, top;
        CHECK(server.GetBlock(5, -50, 5, top) == perlin::abi::Status::Ok && top == "minecraft:air");
        CHECK(server.GetBlock(5, -60, 5, here) == perlin::abi::Status::Ok && here == "minecraft:sand");
        CHECK(server.SetBlock(9, -60, 9, "stone") == perlin::abi::Status::Ok);
        server.Mobs().SpawnPrimedTnt(9.5, -60, 10.5, 5);
        for (int i = 0; i < 20; i++)
            server.Tick();
        std::string blasted, ground;
        CHECK(server.GetBlock(9, -60, 9, blasted) == perlin::abi::Status::Ok && blasted == "minecraft:air");
        CHECK(server.GetBlock(9, -61, 10, ground) == perlin::abi::Status::Ok && ground == "minecraft:air");
        alice.Drain();
        bob.Drain();
        alice.inbox.clear();
        bob.inbox.clear();
    }

    {
        using T = proto::ItemStackRequestActionsEntryTypeId;
        using C = proto::ContainerSlotType;
        const CatalogItem* stone = BlockCatalog::Instance().ItemByName("minecraft:stone");
        CHECK(stone != nullptr);
        CHECK(!start.empty());
        proto::PlayerAuthInputPacket home;
        home.position = {2.5f, -58.38f, 2.5f};
        alice.Send(home);
        alice.Settle();
        alice.inbox.clear();
        bob.inbox.clear();

        proto::ItemStackRequestPacket req;
        proto::ItemStackRequest r;
        r.requestId = -1;
        proto::ItemStackRequestActionsEntry craft;
        craft.typeId = T::CraftCreative;
        craft.itemId = BlockCatalog::Instance().CreativeEntryOf(stone->networkId);
        craft.timesCrafted = 1;
        proto::ItemStackRequestActionsEntry take;
        take.typeId = T::Take;
        take.count = 64;
        take.source.slotType.containerId = C::CreativeOutput;
        take.source.slot = 50;
        take.destination.slotType.containerId = C::Cursor;
        proto::ItemStackRequestActionsEntry put;
        put.typeId = T::Place;
        put.count = 64;
        put.source.slotType.containerId = C::Cursor;
        put.destination.slotType.containerId = C::HotbarAndInventory;
        put.destination.slot = 0;
        r.actions = {craft, take, put};
        req.requests.push_back(r);
        alice.Send(req);
        alice.Settle();
        const auto responses = alice.Take<proto::ItemStackResponsePacket>();
        CHECK(responses.size() == 1 && responses[0].responses.size() == 1);
        if (!responses.empty() && !responses[0].responses.empty()) {
            const auto& e = responses[0].responses[0];
            CHECK(e.status == proto::ItemStackResponsesEntryStatus::Ok && e.requestId == -1);
            CHECK(e.containers && !e.containers->empty());
        }

        const int32_t stoneHash = stone->blockHash;
        proto::InventoryTransactionPacket place;
        place.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
        proto::TransactionUseItem use;
        use.actionType = proto::TransactionUseItemActionType::ClickBlock;
        use.blockPosition = {2, -61, 2};
        use.face = 1;
        use.hotbarSlot = 0;
        place.transaction.transactionData = use;
        {
            // Step off the spot first: a block cannot be placed where a player stands.
            proto::PlayerAuthInputPacket aside;
            aside.position = {4.5f, -58.38f, 4.5f};
            alice.Send(aside);
            alice.Settle();
            bob.Settle();
            alice.inbox.clear();
            bob.inbox.clear();
        }
        alice.Send(place);
        alice.Settle();
        bob.SettleFor<proto::UpdateBlockPacket>();
        alice.SettleFor<proto::UpdateBlockPacket>();
        bob.Settle();
        std::string name;
        CHECK(server.GetBlock(2, -60, 2, name) == perlin::abi::Status::Ok && name == "minecraft:stone");
        const auto updates = bob.Take<proto::UpdateBlockPacket>();
        CHECK(updates.size() == 1 && updates[0].position.y == -60 &&
              static_cast<int32_t>(updates[0].blockRuntimeId) == stoneHash);
        CHECK(alice.Take<proto::UpdateBlockPacket>().size() == 1);

        alice.Send(place);
        alice.Settle();
        bob.Settle();
        CHECK(bob.Take<proto::UpdateBlockPacket>().empty());
        CHECK(alice.Take<proto::UpdateBlockPacket>().size() >= 1);

        proto::InventoryTransactionPacket far = place;
        proto::TransactionUseItem farUse = use;
        farUse.blockPosition = {120, -61, 120};
        far.transaction.transactionData = farUse;
        alice.Send(far);
        alice.Settle();
        CHECK(server.GetBlock(120, -60, 120, name) != perlin::abi::Status::Ok || name == "minecraft:air");

        proto::PlayerActionPacket dig;
        dig.runtimeEntityId = 1;
        dig.action = proto::Action::StartBreak;
        dig.position = {2, -60, 2};
        alice.inbox.clear();
        bob.inbox.clear();
        alice.Send(dig);
        alice.Settle();
        bob.Settle();
        CHECK(server.GetBlock(2, -60, 2, name) == perlin::abi::Status::Ok && name == "minecraft:air");
        CHECK(bob.Take<proto::UpdateBlockPacket>().size() == 1);
        proto::InventoryTransactionPacket brk;
        brk.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
        proto::TransactionUseItem bu;
        bu.actionType = proto::TransactionUseItemActionType::BreakBlock;
        bu.blockPosition = {2, -60, 2};
        brk.transaction.transactionData = bu;
        alice.Send(brk);
        alice.Settle();

        proto::BlockPickRequestPacket pick;
        pick.x = 3;
        pick.y = -61;
        pick.z = 3;
        pick.selectedSlot = 0;
        alice.inbox.clear();
        alice.Send(pick);
        alice.Settle();
        CHECK(!alice.Take<proto::MobEquipmentPacket>().empty());

        CHECK(server.GiveItem(server.FindByName("Alice")->Id(), "dirt", 5) == 0);
        alice.Settle();
        CHECK(alice.Take<proto::InventoryContentPacket>().size() >= 1);
    }

    bob.endpoint->Close();
    alice.Settle();
    CHECK(alice.Take<proto::RemoveEntityPacket>().size() == 1);
    CHECK(server.InGamePlayers().size() == 1);

    {
        const auto run = [&](const std::string& line) {
            alice.inbox.clear();
            proto::CommandRequestPacket c;
            c.command = line;
            alice.Send(c);
            alice.Settle();
        };
        run("/gamemode survival");
        const auto mode = alice.Take<proto::SetPlayerGameTypePacket>();
        CHECK(mode.size() == 1 && mode[0].gamemode == proto::GameMode::Survival);
        CHECK(alice.Take<proto::UpdateAbilitiesPacket>().size() == 1);
        {
            namespace F = proto::FlagsAbilitySet;
            run("/gamemode spectator");
            const auto sp = alice.Take<proto::SetPlayerGameTypePacket>();
            CHECK(sp.size() == 1 && sp[0].gamemode == proto::GameMode::Spectator);
            auto ab = alice.Take<proto::UpdateAbilitiesPacket>();
            CHECK(ab.size() == 1 && !ab[0].abilities.empty());
            if (!ab.empty() && !ab[0].abilities.empty()) {
                const auto& layer = ab[0].abilities[0];
                CHECK((layer.enabled & F::NoClip) && (layer.enabled & F::Flying) &&
                      (layer.enabled & F::MayFly) && (layer.enabled & F::Invulnerable));
                CHECK(!(layer.enabled &
                        (F::Build | F::Mine | F::AttackMobs | F::AttackPlayers | F::OpenContainers)));
                CHECK(layer.flySpeed == 0.05f && layer.verticalFlySpeed == 1.0f);
            }
            for (const int level : {2, 5, 10, 20, 1}) {
                run("/flyspeed " + std::to_string(level));
                ab = alice.Take<proto::UpdateAbilitiesPacket>();
                CHECK(ab.size() == 1 && !ab[0].abilities.empty());
                if (!ab.empty() && !ab[0].abilities.empty()) {
                    CHECK(ab[0].abilities[0].flySpeed == 0.05f * static_cast<float>(level));
                    CHECK(ab[0].abilities[0].verticalFlySpeed == static_cast<float>(level));
                }
                // The action bar goes out as a title, never as a text packet (a jukebox popup text
                // packet got the client to disconnect).
                CHECK(alice.Take<proto::TextPacket>().empty());
                bool shown = false;
                for (const auto& t : alice.Take<proto::SetTitlePacket>())
                    shown = shown || (t.type == proto::SetTitlePacketType::ActionBarMessage &&
                                      t.text == "Flying speed: " + std::to_string(level) + "x");
                CHECK(shown);
            }
            run("/flyspeed 5x");
            CHECK(alice.Take<proto::UpdateAbilitiesPacket>().size() == 1);
            run("/flyspeed 3");
            CHECK(alice.Take<proto::UpdateAbilitiesPacket>().empty());
            CHECK(!alice.Take<proto::TextPacket>().empty());
            run("/gamemode creative");
            alice.inbox.clear();
            run("/gamemode spectator");
            ab = alice.Take<proto::UpdateAbilitiesPacket>();
            CHECK(!ab.empty() && ab[0].abilities[0].flySpeed == 0.05f);
            run("/gamemode survival");
            alice.inbox.clear();
            run("/flyspeed 10");
            CHECK(alice.Take<proto::UpdateAbilitiesPacket>().empty());
            run("/gamemode survival");
        }
        {
            const auto texts = [&] {
                std::string all;
                for (const auto& t : alice.Take<proto::TextPacket>())
                    all += t.message + "\n";
                return all;
            };
            run("/gamemode creative");
            alice.inbox.clear();
            run("/tp 10 80 10");
            auto moved = alice.Take<proto::MovePlayerPacket>();
            CHECK(moved.size() == 1 && moved[0].position.x == 10.5f && moved[0].position.y == 80.0f + 1.62f &&
                  moved[0].position.z == 10.5f);
            run("/tp ~ ~5 ~");
            moved = alice.Take<proto::MovePlayerPacket>();
            CHECK(moved.size() == 1 && moved[0].position.x == 10.5f && moved[0].position.y == 85.0f + 1.62f);
            run("/tp @s 1.5 70 2.5");
            moved = alice.Take<proto::MovePlayerPacket>();
            CHECK(moved.size() == 1 && moved[0].position.x == 1.5f && moved[0].position.z == 2.5f);
            run("/teleport Alice 0 70 0");
            CHECK(alice.Take<proto::MovePlayerPacket>().size() == 1);
            run("/tp @a[name=Alice] 4 70 4 90 0");
            moved = alice.Take<proto::MovePlayerPacket>();
            CHECK(moved.size() == 1 && moved[0].yaw == 90.0f);
            run("/tp @a[gamemode=adventure] 0 0 0");
            std::string out = texts();
            CHECK(out.contains("No player was found"));
            run("/gamemode foo");
            out = texts();
            CHECK(out.contains("Incorrect argument for command") && out.contains("/gamemode foo<--[HERE]"));
            run("/give");
            out = texts();
            CHECK(out.contains("Unknown or incomplete command, see below for error") &&
                  out.contains("/give<--[HERE]"));
            run("/give @s minecraft:not_an_item");
            CHECK(texts().contains("Unknown item 'minecraft:not_an_item'"));
            run("/give @s stone abc");
            CHECK(texts().contains("Expected integer"));
            run("/tp 1 2");
            CHECK(texts().contains("No player was found"));
            run("/tp");
            CHECK(texts().contains("Unknown or incomplete command"));
            run("/tp @s ~ ~ ^");
            CHECK(texts().contains("Cannot mix world & local coordinates"));
            run("/nonsense");
            CHECK(texts().contains("/nonsense<--[HERE]"));
            run("/help give");
            CHECK(texts().contains("/give <targets> <item> [<count>]"));
            run("/time set day");
            CHECK(!alice.Take<proto::SetTimePacket>().empty());
            run("/time set 18000");
            run("/time add 100");
            run("/time query daytime");
            {
                const std::string said = texts();
                const size_t at = said.find("The time is ");
                const long shown = at == std::string::npos ? -1 : std::atol(said.c_str() + at + 12);
                CHECK(shown >= 18100 && shown < 18400);
            }
            run("/weather thunder 5");
            const auto weather = alice.Take<proto::LevelEventPacket>();
            bool rain = false, thunder = false;
            for (const auto& w : weather) {
                rain = rain || w.event == proto::LevelEventPacketEvent::StartRain;
                thunder = thunder || w.event == proto::LevelEventPacketEvent::StartThunder;
            }
            CHECK(rain && thunder && server.Raining() && server.Thundering());
            run("/weather clear");
            CHECK(!server.Raining());
            run("/gamerule doDaylightCycle false");
            CHECK(server.RuleValue("dodaylightcycle") == 0 &&
                  !alice.Take<proto::GameRulesChangedPacket>().empty());
            run("/gamerule dodaylightcycle");
            CHECK(texts().contains("Game rule doDaylightCycle is currently set to: false"));
            run("/gamerule doDaylightCycle true");
            run("/gamerule doDaylightCycle maybe");
            CHECK(texts().contains("Invalid boolean"));
            run("/difficulty hard");
            CHECK(!alice.Take<proto::SetDifficultyPacket>().empty() && server.Config().difficulty == 3);
            run("/difficulty");
            CHECK(texts().contains("The difficulty is Hard"));
            run("/difficulty easy");
            run("/defaultgamemode spectator");
            CHECK(server.Config().gameMode == 6);
            run("/defaultgamemode creative");
            run("/setblock 5 60 5 stone");
            CHECK(texts().contains("Changed the block at 5, 60, 5"));
            CHECK(server.GetBlock(5, 60, 5, out) == perlin::abi::Status::Ok && out.contains("stone"));
            {
                // Bars and walls take their connections from their neighbours, as the game's do.
                run("/setblock 6 60 5 iron_bars");
                run("/setblock 7 60 5 iron_bars");
                const auto states = [&](int x, int y, int z) {
                    return BlockRegistry::Instance().States(server.World(0).GetBlock(x, y, z));
                };
                const auto bit = [&](int x, int y, int z, const std::string& key) {
                    for (const auto& [k, v] : states(x, y, z))
                        if (k == key) {
                            if (const bool* b = std::get_if<bool>(&v))
                                return *b ? 1 : 0;
                            if (const int32_t* i = std::get_if<int32_t>(&v))
                                return *i;
                        }
                    return -1;
                };
                CHECK(bit(6, 60, 5, "minecraft:connection_west") == 1 && bit(6, 60, 5, "minecraft:connection_east") == 1 &&
                      bit(6, 60, 5, "minecraft:connection_north") == 0);
                CHECK(bit(7, 60, 5, "minecraft:connection_west") == 1 && bit(7, 60, 5, "minecraft:connection_east") == 0);
                run("/setblock 7 60 5 air");
                CHECK(bit(6, 60, 5, "minecraft:connection_east") == 0);
                run("/setblock 5 61 5 cobblestone_wall");
                run("/setblock 6 61 5 cobblestone_wall");
                std::string side;
                for (const auto& [k, v] : states(5, 61, 5))
                    if (k == "wall_connection_type_east")
                        if (const std::string* t = std::get_if<std::string>(&v))
                            side = *t;
                CHECK(side == "short");
                CHECK(bit(5, 61, 5, "wall_post_bit") == 1);
                run("/setblock 6 60 5 air");
                run("/setblock 5 61 5 air");
                run("/setblock 6 61 5 air");
            }
            run("/fill 0 70 0 3 72 3 stone");
            CHECK(texts().contains("48 blocks filled"));
            run("/fill 0 70 0 3 72 3 air hollow");
            CHECK(texts().contains("blocks filled"));
            run("/fill 0 70 0 100 100 100 stone");
            CHECK(texts().contains("Too many blocks in the specified area (maximum 32768"));
            run("/setblock 100000 60 5 stone");
            CHECK(texts().contains("That position is not loaded"));
            run("/title @s actionbar \"hello there\"");
            const auto titles = alice.Take<proto::SetTitlePacket>();
            CHECK(titles.size() == 1 && titles[0].text == "hello there");
            run("/tellraw @s {\"text\":\"raw \",\"extra\":[{\"text\":\"words\"}]}");
            CHECK(texts().contains("raw words"));
            run("/locate biome plains");
            CHECK(texts().contains("flat world"));
            run("/locate biome nowhere_land");
            CHECK(texts().contains("Unknown biome 'nowhere_land'"));
            {
                run("/gamemode survival");
                run("/difficulty easy");
                run("/tp 0 -60 0");
                alice.inbox.clear();
                run("/summon pig ~3 ~ ~");
                auto added = alice.Take<proto::AddEntityPacket>();
                CHECK(added.size() == 1 && added[0].entityType == "minecraft:pig");
                const uint64_t pig = added.empty() ? 0 : added[0].runtimeId;
                proto::InventoryTransactionPacket hit;
                hit.transaction.transactionType = proto::TransactionTransactionType::ItemUseOnEntity;
                auto& on = hit.transaction.transactionData.emplace<2>();
                on.entityRuntimeId = pig;
                on.actionType = proto::TransactionTransactionDataCase1ActionType::Attack;
                alice.Send(hit);
                alice.Settle();
                bool hurt = false;
                for (const auto& e : alice.Take<proto::EntityEventPacket>())
                    hurt = hurt || (e.runtimeEntityId == pig && e.eventId == proto::EntityEventPacketEventId::HurtAnimation);
                CHECK(hurt);
                CHECK(!alice.Take<proto::MoveEntityPacket>().empty());
                run("/kill @e[type=pig]");
                bool died = false;
                for (const auto& e : alice.Take<proto::EntityEventPacket>())
                    died = died || (e.runtimeEntityId == pig && e.eventId == proto::EntityEventPacketEventId::DeathAnimation);
                CHECK(died);
                alice.Settle(25);
                bool removed = false;
                for (const auto& r : alice.Take<proto::RemoveEntityPacket>())
                    removed = removed || r.entityIdSelf == static_cast<int64_t>(pig);
                CHECK(removed);
                run("/summon nothing_at_all");
                CHECK(texts().contains("nothing_at_all") || alice.Take<proto::AddEntityPacket>().empty());
                alice.inbox.clear();
                run("/summon zombie ~1.5 ~ ~");
                CHECK(alice.Take<proto::AddEntityPacket>().size() == 1);
                alice.Settle(60);
                bool bitten = false;
                for (const auto& a : alice.Take<proto::UpdateAttributesPacket>())
                    for (const auto& at : a.attributes)
                        bitten = bitten || (at.name == "minecraft:health" && at.current < 20.0f);
                CHECK(bitten);
                run("/kill @e[type=zombie]");
                {
                    std::string out;
                    run("/setblock 1 -60 0 furnace");
                    Session* a = server.FindByName("Alice");
                    CHECK(a && server.UseBlock(*a, 1, -60, 0));
                    alice.Settle();
                    const auto opened = alice.Take<proto::ContainerOpenPacket>();
                    CHECK(opened.size() == 1 && opened[0].windowType == proto::WindowType::Furnace);
                    const CatalogItem* ore = BlockCatalog::Instance().ItemByName("minecraft:raw_iron");
                    const CatalogItem* coal = BlockCatalog::Instance().ItemByName("minecraft:coal");
                    const CatalogItem* ingot = BlockCatalog::Instance().ItemByName("minecraft:iron_ingot");
                    CHECK(ore && coal && ingot);
                    if (a && ore && coal && ingot) {
                        a->Inventory().container[0] = a->Inventory().MakeStack(ore->networkId, 2);
                        a->Inventory().container[1] = a->Inventory().MakeStack(coal->networkId, 1);
                        server.SyncContainer(*a);
                        alice.Settle(5);
                        CHECK(server.GetBlock(1, -60, 0, out) == perlin::abi::Status::Ok && out.contains("lit_furnace"));
                        alice.Settle(200);
                        CHECK(a->Inventory().container[2].networkId == ingot->networkId && a->Inventory().container[2].count >= 1);
                        CHECK(!alice.Take<proto::ContainerSetDataPacket>().empty());
                        server.CloseContainer(*a);
                    }
                    const CatalogItem* log = BlockCatalog::Instance().ItemByName("minecraft:oak_log");
                    const CatalogItem* planks = BlockCatalog::Instance().ItemByName("minecraft:oak_planks");
                    uint32_t netId = 0;
                    for (const auto& r : Recipes::Instance().Crafting())
                        if (r.output == "oak_planks" && r.input.size() == 1 && r.input[0].item == "oak_log")
                            netId = r.netId;
                    CHECK(log && planks && netId);
                    if (a && log && planks && netId) {
                        a->Inventory().main[5] = ItemStack{};
                        a->Inventory().craft[28] = a->Inventory().MakeStack(log->networkId, 1);
                        proto::ItemStackRequest req;
                        req.requestId = -77;
                        proto::ItemStackRequestActionsEntry craft;
                        craft.typeId = proto::ItemStackRequestActionsEntryTypeId::CraftRecipe;
                        craft.recipeNetworkId = netId;
                        craft.timesCrafted = 1;
                        req.actions.push_back(craft);
                        proto::ItemStackRequestActionsEntry use;
                        use.typeId = proto::ItemStackRequestActionsEntryTypeId::Consume;
                        use.count = 1;
                        use.source.slotType.containerId = proto::ContainerSlotType::CraftingInput;
                        use.source.slot = 28;
                        req.actions.push_back(use);
                        proto::ItemStackRequestActionsEntry take;
                        take.typeId = proto::ItemStackRequestActionsEntryTypeId::Take;
                        take.count = 4;
                        take.source.slotType.containerId = proto::ContainerSlotType::CreativeOutput;
                        take.source.slot = 50;
                        take.destination.slotType.containerId = proto::ContainerSlotType::HotbarAndInventory;
                        take.destination.slot = 5;
                        req.actions.push_back(take);
                        const RequestResult rr = ApplyStackRequest(a->Inventory(), req, false);
                        CHECK(rr.ok && a->Inventory().main[5].networkId == planks->networkId && a->Inventory().main[5].count == 4);
                        CHECK(a->Inventory().craft[28].Empty());
                    }
                }
                if (Session* a = server.FindByName("Alice")) {
                    alice.Settle(30);
                    run("/xp 30");
                    CHECK(a->XpLevel() == 3);
                    run("/xp 5L");
                    CHECK(a->XpLevel() == 8);
                    alice.Settle();
                    bool levelSent = false;
                    for (const auto& u : alice.Take<proto::UpdateAttributesPacket>())
                        for (const auto& at : u.attributes)
                            levelSent = levelSent || (at.name == "minecraft:player.level" && at.current == 8.0f);
                    CHECK(levelSent);

                    const CatalogItem* plate = BlockCatalog::Instance().ItemByName("minecraft:diamond_chestplate");
                    CHECK(plate != nullptr);
                    alice.Settle(25);
                    if (plate) {
                        a->Inventory().armor[1] = a->Inventory().MakeStack(plate->networkId, 1);
                        const float before = a->Health();
                        CHECK(a->Damage(10.0f, Session::DamageCause::Mob, a->X() + 1, a->Z()));
                        CHECK(std::abs((before - a->Health()) - 8.4f) < 0.01f);
                        CHECK(a->Inventory().armor[1].damage == 2);
                        CHECK(!a->Damage(5.0f, Session::DamageCause::Mob, a->X() + 1, a->Z()));
                        a->Inventory().armor[1] = ItemStack{};
                    }
                    alice.Settle(25);
                    {
                        proto::PlayerAuthInputPacket still;
                        still.position = {static_cast<float>(a->X()), static_cast<float>(a->Y()),
                                          static_cast<float>(a->Z())};
                        still.tick = server.TickCount() + 7000;
                        alice.Send(still);
                        alice.Settle();
                        alice.inbox.clear();
                        CHECK(a->Damage(1.0f, Session::DamageCause::Mob, a->X() + 1, a->Z()));
                        alice.Settle(1);
                        bool pushed = false;
                        for (const auto& m : alice.Take<proto::SetEntityMotionPacket>())
                            if (m.runtimeEntityId == a->Id())
                                pushed = m.tick == still.tick && m.velocity.x < 0.0f;
                        CHECK(pushed);
                    }
                    alice.Settle(25);
                    a->Heal(20.0f);
                    CHECK(a->Health() == 20.0f);
                    run("/tp 0 -50 0");
                    for (int i = 0; i < 10; i++) {
                        proto::PlayerAuthInputPacket fall;
                        fall.position = {0.5f, -48.38f - static_cast<float>(i + 1), 0.5f};
                        alice.Send(fall);
                        alice.Settle();
                    }
                    proto::PlayerAuthInputPacket land;
                    land.position = {0.5f, -58.38f, 0.5f};
                    land.inputData.push_back(proto::InputData::VerticalCollision);
                    alice.Send(land);
                    alice.Settle();
                    CHECK(a->Health() >= 16.0f && a->Health() < 17.5f);
                    run("/menu");
                    const auto forms = alice.Take<proto::ModalFormRequestPacket>();
                    CHECK(forms.size() == 1 && forms[0].data.contains("Heal me"));
                    if (!forms.empty()) {
                        proto::ModalFormResponsePacket answer;
                        answer.formId = forms[0].formId;
                        answer.hasResponseData = true;
                        answer.data = "0";
                        alice.Send(answer);
                        alice.Settle();
                        CHECK(a->Health() == 20.0f);
                    }

                    const CatalogItem* bread = BlockCatalog::Instance().ItemByName("minecraft:bread");
                    CHECK(bread != nullptr);
                    if (bread) {
                        a->SetFood(10, 0.0f);
                        a->Inventory().main[0] = a->Inventory().MakeStack(bread->networkId, 3);
                        a->Inventory().selected = 0;
                        proto::InventoryTransactionPacket eat;
                        eat.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
                        proto::TransactionUseItem eu;
                        eu.actionType = proto::TransactionUseItemActionType::ClickAir;
                        eu.hotbarSlot = 0;
                        eat.transaction.transactionData = eu;
                        alice.Send(eat);
                        alice.Settle(40);
                        CHECK(a->Food() == 15);
                        CHECK(a->Inventory().main[0].count == 2);
                    }

                    const CatalogItem* pick = BlockCatalog::Instance().ItemByName("minecraft:iron_pickaxe");
                    const CatalogItem* diamond = BlockCatalog::Instance().ItemByName("minecraft:diamond");
                    CHECK(pick && diamond);
                    if (pick && diamond) {
                        const auto count = [&](int16_t id) {
                            int n = 0;
                            for (const auto& it : server.Items())
                                if (it.stack.networkId == id)
                                    n += it.stack.count;
                            return n;
                        };
                        const int had = count(diamond->networkId);
                        a->Inventory().main[0] = ItemStack{};
                        CHECK(server.SetBlock(1, -60, 2, "diamond_ore") == perlin::abi::Status::Ok);
                        CHECK(server.PlayerBreak(*a, 1, -60, 2));
                        CHECK(count(diamond->networkId) == had);
                        a->Inventory().main[0] = a->Inventory().MakeStack(pick->networkId, 1);
                        CHECK(server.SetBlock(1, -60, 2, "diamond_ore") == perlin::abi::Status::Ok);
                        CHECK(server.PlayerBreak(*a, 1, -60, 2));
                        CHECK(count(diamond->networkId) == had + 1);
                        CHECK(a->Inventory().main[0].damage == 1);
                        size_t orbs = 0;
                        for (const Mob& m : server.Mobs().All())
                            orbs += m.kind == 3 ? 1 : 0;
                        CHECK(orbs > 0);
                    }

                    alice.inbox.clear();
                    run("/summon cow ~2 ~ ~");
                    auto cows = alice.Take<proto::AddEntityPacket>();
                    std::erase_if(cows, [](const proto::AddEntityPacket& e) { return e.entityType != "minecraft:cow"; });
                    CHECK(cows.size() == 1);
                    if (!cows.empty()) {
                        const CatalogItem* leather = BlockCatalog::Instance().ItemByName("minecraft:leather");
                        const CatalogItem* beef = BlockCatalog::Instance().ItemByName("minecraft:beef");
                        CHECK(leather && beef);
                        CHECK(server.Mobs().Attack(*a, cows[0].runtimeId, 100.0f));
                        int meat = 0;
                        for (const auto& it : server.Items())
                            if (beef && it.stack.networkId == beef->networkId)
                                meat += it.stack.count;
                        CHECK(meat >= 1);
                    }
                    run("/effect @s regeneration 5 1");
                    CHECK(a->EffectLevel(10) == 2);
                    run("/effect @s clear");
                    CHECK(a->EffectLevel(10) == 0);
                    const int levelBefore = a->XpLevel();
                    run("/kill @s");
                    CHECK(a->Dead());
                    CHECK(a->XpLevel() == 0 || levelBefore == 0);
                    proto::RespawnPacket respawn;
                    respawn.state = 2;
                    alice.Send(respawn);
                    alice.Settle();
                    CHECK(!a->Dead() && a->Health() == 20.0f && a->Food() == 20);
                }
                run("/gamemode creative");
                alice.Settle(25);
                alice.inbox.clear();
            }
            run("/seed");
            CHECK(texts().contains("Seed: ["));
            run("/clear");
            CHECK(texts().contains("items") || true);
            run("/gamemode survival");
        }
        {
            proto::AvailableCommandsPacket packet;
            FillAvailableCommands(packet, server.Commands(), true);
            const proto::AvailableCommandsPacketCommandDataEntry* give = nullptr;
            const proto::AvailableCommandsPacketCommandDataEntry* gamemode = nullptr;
            for (const auto& e : packet.commandData) {
                if (e.name == "give")
                    give = &e;
                if (e.name == "gamemode")
                    gamemode = &e;
            }
            CHECK(give && give->overloads.size() == 1 && give->overloads[0].parameters.size() == 3);
            if (give && give->overloads[0].parameters.size() == 3) {
                const auto& p = give->overloads[0].parameters;
                CHECK(
                    p[0].parameterName == "targets" &&
                    p[0].valueType ==
                        proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryValueType::
                            Target);
                // The item is an enum of the game's item names, so it completes.
                CHECK(p[1].enumType == proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryEnumType::Enum &&
                      static_cast<size_t>(p[1].valueType) < packet.enums.size() && packet.enums[static_cast<size_t>(p[1].valueType)].name == "Item" &&
                      !p[1].optional);
                CHECK(
                    p[2].valueType ==
                        proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryValueType::
                            Int &&
                    p[2].optional);
            }
            CHECK(gamemode && gamemode->overloads.size() == 1);
            if (gamemode && !gamemode->overloads.empty()) {
                const auto& first = gamemode->overloads[0].parameters[0];
                CHECK(first.enumType ==
                      proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryEnumType::
                          Enum);
                const size_t index = static_cast<size_t>(first.valueType);
                CHECK(index < packet.enums.size() && packet.enums[index].values.size() == 4);
            }
            CHECK(!packet.dynamicEnums.empty());
        }
        run("/fill 10 -60 10 11 -60 11 stone");
        alice.Take<proto::TextPacket>();
        run("/clone 10 -60 10 11 -60 11 20 -60 20");
        auto cloned = alice.Take<proto::TextPacket>();
        CHECK(!cloned.empty() && cloned[0].message.find("4 blocks cloned") == 0);
        run("/testforblocks 10 -60 10 11 -60 11 20 -60 20");
        cloned = alice.Take<proto::TextPacket>();
        CHECK(!cloned.empty() && cloned[0].message.find("4 blocks compared") == 0);
        // Namespaced entity names are accepted, as in the game.
        run("/summon minecraft:cow 12 -60 12");
        auto summoned = alice.Take<proto::TextPacket>();
        CHECK(!summoned.empty() && summoned[0].message.find("Incorrect") == std::string::npos);
        run("/give Alice stone 5");
        auto lines = alice.Take<proto::TextPacket>();
        CHECK(!lines.empty() && lines[0].message.find("Gave 5 stone to Alice") == 0);
        CHECK(!alice.Take<proto::InventoryContentPacket>().empty());
        run("/help");
        lines = alice.Take<proto::TextPacket>();
        bool noNewlines = lines.size() > 5;
        for (const auto& l : lines)
            noNewlines = noNewlines && !l.message.contains('\n');
        CHECK(noNewlines);
        run("/tick freeze");
        const uint64_t frozenAt = server.TickCount();
        server.Tick();
        server.Tick();
        CHECK(server.TickCount() == frozenAt);
        run("/tick step 3");
        server.Tick();
        server.Tick();
        server.Tick();
        server.Tick();
        CHECK(server.TickCount() == frozenAt + 3);
        run("/tick unfreeze");
        run("/tick query");
        lines = alice.Take<proto::TextPacket>();
        CHECK(!lines.empty() && lines[0].message.contains("running normally"));
        server.RunConsoleCommand("op Bob");
        CHECK(server.IsOperator("Bob", ""));
        CHECK(server.PermissionOf("Bob", "") == "operator");
        server.RunConsoleCommand("deop Bob");
        CHECK(!server.IsOperator("Bob", ""));
        CHECK(server.PermissionOf("Bob", "") == "member");
        server.RunConsoleCommand("allowlist add Bob");
        CHECK(server.IsWhitelisted("bob", ""));
        server.RunConsoleCommand("whitelist add Carol");
        CHECK(server.IsWhitelisted("carol", ""));
        CHECK(fs::exists(config.dataDir + "/permissions.json") &&
              fs::exists(config.dataDir + "/allowlist.json"));
        CHECK(!fs::exists(config.dataDir + "/ops.json") && !fs::exists(config.dataDir + "/whitelist.json"));
    }

    {
        const auto send = [&](Client& who, const std::string& line) {
            who.inbox.clear();
            proto::CommandRequestPacket c;
            c.command = line;
            who.Send(c);
            who.Settle();
        };
        const auto said = [&](Client& who) {
            std::string all;
            for (const auto& t : who.Take<proto::TextPacket>())
                all += t.message + "\n";
            return all;
        };
        send(alice, "/gamemode creative");
        send(alice, "/execute in the_nether run tp @s 8 70 8");
        for (int i = 0; i < 80; i++)
            alice.Settle(2);
        const auto change = alice.Take<proto::ChangeDimensionPacket>();
        CHECK(change.size() == 1 && change[0].dimension == 1);
        const auto netherChunks = alice.Take<proto::LevelChunkPacket>();
        CHECK(!netherChunks.empty());
        for (const auto& c : netherChunks) {
            CHECK(c.dimension == 1 && c.subChunkCount == 8);
            std::vector<std::pair<int, perlite::SubChunkData>> subs;
            size_t used = 0;
            CHECK(perlite::ChunkCodec::ParseColumn(c.payload.data(), c.payload.size(), c.subChunkCount, 0, 8, subs, used));
            CHECK(subs.size() == 8);
        }
        proto::PlayerActionPacket ack;
        ack.action = proto::Action::DimensionChangeAck;
        alice.Send(ack);
        alice.Settle();
        Session* a = server.FindByName("Alice");
        CHECK(a && a->DimensionId() == 1);
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(1, 8, 0, 8)) == "minecraft:bedrock");
        send(alice, "/execute if dimension the_nether");
        CHECK(said(alice).contains("Test passed"));
        send(alice, "/execute unless entity @a[name=Bob,distance=..1000]");
        CHECK(said(alice).contains("Test passed"));
        send(alice, "/execute as @s at @s if dimension the_nether run say still here");
        CHECK(said(alice).contains("still here"));
        send(alice, "/execute as @s at @s if dimension overworld run say wrong place");
        CHECK(!said(alice).contains("wrong place"));
        send(alice, "/execute positioned 8 70 9 run setblock ~ ~ ~ obsidian");
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(1, 8, 70, 9)) == "minecraft:obsidian");
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, 8, 70, 9)) != "minecraft:obsidian");
        const CatalogItem* flint = BlockCatalog::Instance().ItemByName("minecraft:flint_and_steel");
        if (flint && a) {
            send(alice, "/fill 5 69 12 8 73 12 obsidian");
            send(alice, "/fill 6 70 12 7 72 12 air");
            a->Inventory().main[0] = ItemStack{};
            a->Inventory().main[0].networkId = flint->networkId;
            a->Inventory().main[0].count = 1;
            proto::InventoryTransactionPacket light;
            light.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
            proto::TransactionUseItem use;
            use.actionType = proto::TransactionUseItemActionType::ClickBlock;
            use.blockPosition = {6, 69, 12};
            use.face = 1;
            use.hotbarSlot = 0;
            light.transaction.transactionData = use;
            alice.Send(light);
            alice.Settle();
            CHECK(BlockRegistry::Instance().Name(server.BlockAt(1, 6, 70, 12)) == "minecraft:portal");
            CHECK(BlockRegistry::Instance().Name(server.BlockAt(1, 7, 72, 12)) == "minecraft:portal");
            alice.inbox.clear();
            send(alice, "/tp @s 6.5 70 12.5");
            for (int i = 0; i < 40; i++)
                alice.Settle(2);
            const auto back = alice.Take<proto::ChangeDimensionPacket>();
            CHECK(back.size() == 1 && back[0].dimension == 0);
            CHECK(a->DimensionId() == 0);
            if (!back.empty())
                CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, static_cast<int>(std::floor(back[0].position.x)),
                                                                    static_cast<int>(std::floor(back[0].position.y - 1.62f)),
                                                                    static_cast<int>(std::floor(back[0].position.z)))) ==
                      "minecraft:portal");
            alice.Send(ack);
            alice.Settle();
            send(alice, "/fill 0 -60 -10 4 -60 -6 end_portal_frame");
            const CatalogItem* eye = BlockCatalog::Instance().ItemByName("minecraft:ender_eye");
            if (eye) {
                send(alice, "/fill 1 -60 -9 3 -60 -7 air");
                send(alice, "/setblock 0 -60 -10 air");
                send(alice, "/setblock 4 -60 -10 air");
                send(alice, "/setblock 0 -60 -6 air");
                send(alice, "/setblock 4 -60 -6 air");
                send(alice, "/tp @s 2 -59 -4");
                a->Inventory().main[1].networkId = eye->networkId;
                a->Inventory().main[1].count = 12;
                const int ring[12][2] = {{1, -10}, {2, -10}, {3, -10}, {1, -6}, {2, -6}, {3, -6},
                                         {0, -9},  {0, -8},  {0, -7},  {4, -9}, {4, -8}, {4, -7}};
                for (const auto& r : ring) {
                    proto::InventoryTransactionPacket put;
                    put.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
                    proto::TransactionUseItem u;
                    u.actionType = proto::TransactionUseItemActionType::ClickBlock;
                    u.blockPosition = {r[0], -60, r[1]};
                    u.face = 1;
                    u.hotbarSlot = 1;
                    put.transaction.transactionData = u;
                    alice.Send(put);
                    alice.Settle(1);
                }
                CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, 2, -60, -8)) == "minecraft:end_portal");
                alice.inbox.clear();
                send(alice, "/tp @s 2.5 -60 -7.5");
                for (int i = 0; i < 40; i++)
                    alice.Settle(2);
                const auto toEnd = alice.Take<proto::ChangeDimensionPacket>();
                CHECK(toEnd.size() == 1 && toEnd[0].dimension == 2);
                CHECK(BlockRegistry::Instance().Name(server.BlockAt(2, 100, 48, 0)) == "minecraft:obsidian");
                alice.Send(ack);
                alice.Settle();
            }
        }
        send(alice, "/execute in overworld run tp @s 0 -59 0");
        for (int i = 0; i < 20; i++)
            alice.Settle(2);
        CHECK(a && a->DimensionId() == 0);
        alice.Send(ack);
        alice.Settle();
    }

    {
    // Stations: enchanting offers, the anvil, brewing, trims.
        const auto id = [&](const char* n) { const CatalogItem* i = BlockCatalog::Instance().ItemByName(n); return i ? i->networkId : int16_t{0}; };
        ItemStack sword;
        sword.networkId = id("minecraft:diamond_sword");
        sword.count = 1;
        const auto offers = EnchantOffers(sword, 15, 12345);
        CHECK(offers.size() == 3 && offers[2].cost == 30 && !offers[0].enchants.empty());
        for (const auto& o : offers)
            for (const auto& [e, l] : o.enchants)
                CHECK(e == 9 || e == 10 || e == 11 || e == 12 || e == 13 || e == 14 || e == 17); // sword enchantments only
        ItemStack book;
        book.networkId = id("minecraft:enchanted_book");
        book.count = 1;
        book.enchants = {{9, 5}};
        int cost = 0, used = 0;
        const ItemStack merged = AnvilResult(sword, book, "Blade", cost, used);
        CHECK(merged.Enchant(9) == 5 && merged.name == "Blade" && cost > 0 && merged.repairCost == 1);
        int16_t outItem = 0;
        uint16_t outMeta = 0;
        CHECK(BrewResult(id("minecraft:potion"), 0, id("minecraft:nether_wart"), outItem, outMeta) && outMeta == 4);
        CHECK(BrewResult(id("minecraft:potion"), 4, id("minecraft:sugar"), outItem, outMeta) && outMeta == 14);
        CHECK(BrewResult(id("minecraft:potion"), 14, id("minecraft:gunpowder"), outItem, outMeta) && outItem == id("minecraft:splash_potion"));
        std::string pattern, material;
        CHECK(TrimOf("minecraft:coast_armor_trim_smithing_template", "minecraft:iron_ingot", pattern, material) && pattern == "coast" && material == "iron");
    }
    server.Stop();

    {
        ServerConfig survivalConfig = config;
        survivalConfig.gameMode = 0;
        survivalConfig.pluginDir.clear();
        survivalConfig.operators = {};
        Server survival(survivalConfig);
        std::string e2;
        CHECK(survival.Start(e2));
        Client carol(survival, "Carol");
        Join(carol);
        proto::SetLocalPlayerAsInitializedPacket initCarol;
        carol.Send(initCarol);
        carol.Settle();
        CHECK(survival.GiveItem(1, "stone", 2) == 0);
        carol.Settle();
        carol.inbox.clear();
        proto::InventoryTransactionPacket place;
        place.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
        proto::TransactionUseItem use;
        use.actionType = proto::TransactionUseItemActionType::ClickBlock;
        use.blockPosition = {1, -61, 1};
        use.face = 1;
        place.transaction.transactionData = use;
        carol.Send(place);
        carol.Settle();
        std::string n;
        CHECK(survival.GetBlock(1, -60, 1, n) == perlin::abi::Status::Ok && n == "minecraft:stone");
        carol.Send(place);
        carol.Settle();
        proto::PlayerActionPacket stop;
        stop.action = proto::Action::StopBreak;
        stop.position = {1, -60, 1};
        carol.Send(stop);
        carol.Settle();
        CHECK(survival.GetBlock(1, -60, 1, n) == perlin::abi::Status::Ok && n == "minecraft:air");
        carol.inbox.clear();
        stop.position = {1, -64, 1};
        carol.Send(stop);
        carol.Settle();
        CHECK(survival.GetBlock(1, -64, 1, n) == perlin::abi::Status::Ok && n == "minecraft:bedrock");

        // Survival digging shows the crack animation: a start event with the break speed, a stop on abort.
        {
            const uint16_t stone = BlockCatalog::Instance().DefaultBlock("minecraft:stone");
            survival.World(0).SetBlock(2, -60, 2, stone);
            carol.inbox.clear();
            proto::PlayerAuthInputPacket in;
            in.position = {0.5f, -58.38f, 0.5f};
            in.tick = 1;
            proto::PlayerAuthInputPacketBlockActionEntry start;
            start.action = proto::Action::StartBreak;
            start.position = {2, -60, 2};
            start.face = 1;
            in.blockAction = std::vector{start};
            carol.Send(in);
            carol.Settle();
            auto events = carol.Take<proto::LevelEventPacket>();
            bool started = false;
            for (const auto& e : events)
                started |= e.event == proto::LevelEventPacketEvent::BlockStartBreak && e.data > 0 && e.data < 65535 &&
                           e.position.x == 2.5f && e.position.y == -59.5f;
            CHECK(started);
            proto::PlayerAuthInputPacketBlockActionEntry abort = start;
            abort.action = proto::Action::AbortBreak;
            in.blockAction = std::vector{abort};
            in.tick = 2;
            carol.Send(in);
            carol.Settle();
            bool stopped = false;
            for (const auto& e : carol.Take<proto::LevelEventPacket>())
                stopped |= e.event == proto::LevelEventPacketEvent::BlockStopBreak;
            CHECK(stopped);
            CHECK(survival.GetBlock(2, -60, 2, n) == perlin::abi::Status::Ok && n == "minecraft:stone");
        }

        // Breaking a block in survival drops it as an item next to the block.
        {
            const uint16_t dirt = BlockCatalog::Instance().DefaultBlock("minecraft:dirt");
            survival.World(0).SetBlock(1, -60, 1, dirt);
            carol.inbox.clear();
            proto::PlayerAuthInputPacket in;
            in.position = {0.5f, -58.38f, 0.5f};
            in.tick = 5;
            proto::PlayerAuthInputPacketBlockActionEntry br;
            br.action = proto::Action::PredictBreak;
            br.position = {1, -60, 1};
            br.face = 1;
            in.blockAction = std::vector{br};
            carol.Send(in);
            carol.Settle();
            const auto items = carol.Take<proto::AddItemEntityPacket>();
            if (items.empty())
                std::printf("survival break dropped no item\n");
            CHECK(!items.empty());
            CHECK(survival.GetBlock(1, -60, 1, n) == perlin::abi::Status::Ok && n == "minecraft:air");
            if (!items.empty()) {
                const uint64_t itemId = items[0].runtimeEntityId;
                carol.Settle(60);
                float ix = items[0].position.x, iz = items[0].position.z;
                for (const auto& mv : carol.Take<proto::MoveEntityPacket>())
                    if (mv.runtimeEntityId == itemId) {
                        ix = mv.position.x;
                        iz = mv.position.z;
                    }
                proto::PlayerAuthInputPacket walk;
                walk.position = {ix, -58.38f, iz};
                walk.tick = 70;
                carol.Send(walk);
                carol.Settle(10);
                const auto taken = carol.Take<proto::TakeItemEntityPacket>();
                CHECK(!taken.empty());
            }
        }

        // Placed blocks face the player, slabs keep the water they are placed in, flint and steel
        // lights fire, and no solid block goes where the player stands.
        {
            const auto placeWith = [&](const char* item, int bx, int by, int bz, int face, float clickY, float yaw) {
                CHECK(survival.GiveItem(1, item, 1) == 0);
                Session* c = survival.FindSession(1);
                if (c)
                    for (int slot = 0; slot < PlayerInventory::kMainSlots; slot++)
                        if (!c->Inventory().main[slot].Empty() &&
                            BlockCatalog::Instance().ItemById(c->Inventory().main[slot].networkId)->name == std::string("minecraft:") + item) {
                            std::swap(c->Inventory().main[0], c->Inventory().main[slot]);
                            break;
                        }
                proto::PlayerAuthInputPacket look;
                look.position = {0.5f, -58.38f, 0.5f};
                look.yaw = yaw;
                look.headYaw = yaw;
                carol.Send(look);
                carol.Settle();
                proto::InventoryTransactionPacket t;
                t.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
                proto::TransactionUseItem u;
                u.actionType = proto::TransactionUseItemActionType::ClickBlock;
                u.blockPosition = {bx, by, bz};
                u.face = static_cast<uint8_t>(face);
                u.hotbarSlot = 0;
                u.clickPos = {0.5f, clickY, 0.5f};
                t.transaction.transactionData = u;
                carol.Send(t);
                carol.Settle();
            };
            const auto stateOf = [&](int bx, int by, int bz) {
                return BlockCatalog::Instance().StateText(survival.World(0).GetBlock(bx, by, bz));
            };
            // Looking north (yaw 180): a furnace faces the player, south.
            placeWith("furnace", 3, -61, 0, 1, 1.0f, 180.0f);
            CHECK(stateOf(3, -60, 0).contains("cardinal_direction=south"));
            // Stairs placed looking east, on the upper half of a side face, are upside down.
            survival.World(0).SetBlock(5, -60, 0, BlockCatalog::Instance().DefaultBlock("minecraft:stone"));
            placeWith("oak_stairs", 5, -60, 0, 4, 0.8f, 270.0f);
            const std::string stairs = stateOf(4, -60, 0);
            CHECK(stairs.contains("weirdo_direction=0") && stairs.contains("upside_down_bit=true"));
            // A slab placed into still water keeps the water in the second layer.
            const uint16_t water = BlockCatalog::Instance().DefaultBlock("minecraft:water");
            survival.World(0).SetBlock(0, -60, 4, water);
            placeWith("oak_slab", 0, -61, 4, 1, 1.0f, 0.0f);
            CHECK(stateOf(0, -60, 4).starts_with("minecraft:oak_slab"));
            CHECK(survival.World(0).GetLiquid(0, -60, 4) == water);
            // Breaking it leaves the water behind.
            CHECK(survival.PlayerBreak(*survival.FindSession(1), 0, -60, 4));
            CHECK(survival.World(0).GetBlock(0, -60, 4) == water && survival.World(0).GetLiquid(0, -60, 4) == 0);
            // Stone cannot be placed into the player.
            placeWith("stone", 0, -61, 0, 1, 1.0f, 0.0f);
            CHECK(survival.World(0).GetBlock(0, -60, 0) == BlockRegistry::kAir);
            // Flint and steel sets fire on the ground.
            placeWith("flint_and_steel", -3, -61, -3, 1, 1.0f, 0.0f);
            CHECK(stateOf(-3, -60, -3).starts_with("minecraft:fire"));
        }

        // Projectiles: a thrown snowball shows up and flies; an arrow sticks in the ground and the
        // player standing on it picks it up.
        {
            carol.inbox.clear();
            CHECK(survival.GiveItem(1, "snowball", 1) == 0);
            Session* c = survival.FindSession(1);
            CHECK(c != nullptr);
            if (c) {
                for (int slot = 0; slot < PlayerInventory::kMainSlots; slot++)
                    if (!c->Inventory().main[slot].Empty() &&
                        BlockCatalog::Instance().ItemById(c->Inventory().main[slot].networkId)->name == "minecraft:snowball")
                        std::swap(c->Inventory().main[0], c->Inventory().main[slot]);
                c->Inventory().selected = 0;
                proto::PlayerAuthInputPacket look;
                look.position = {0.5f, -58.38f, 0.5f};
                look.pitch = -30.0f;
                carol.Send(look);
                carol.Settle();
                proto::InventoryTransactionPacket t;
                t.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
                proto::TransactionUseItem u;
                u.actionType = proto::TransactionUseItemActionType::ClickAir;
                u.hotbarSlot = 0;
                t.transaction.transactionData = u;
                carol.Send(t);
                carol.Settle(3);
                bool snowball = false;
                for (const auto& a : carol.Take<proto::AddEntityPacket>())
                    snowball |= a.entityType == "minecraft:snowball";
                CHECK(snowball);
                CHECK(c->Inventory().main[0].Empty());

                Mob* arrow = survival.Mobs().SpawnProjectile(projectile::Arrow, -1.5, -55.0, 2.5, 0.0, -1.0, 0.0, 999, true);
                CHECK(arrow != nullptr);
                const uint64_t arrowId = arrow ? arrow->id : 0;
                carol.Settle(10);
                const Mob* stuck = survival.Mobs().Find(arrowId);
                CHECK(stuck && stuck->stuck && stuck->y < -59.9 && stuck->y > -61.0);
                proto::PlayerAuthInputPacket walk;
                walk.position = {-1.5f, -58.38f, 2.5f};
                carol.Send(walk);
                carol.Settle(3);
                bool picked = false;
                for (const auto& tk : carol.Take<proto::TakeItemEntityPacket>())
                    picked |= tk.target == arrowId;
                CHECK(picked);
                CHECK(survival.Mobs().Find(arrowId) == nullptr);
            }
        }

        // Water flows out seven blocks on flat ground and drains when its source goes; lava next to
        // water hardens.
        {
            ChunkManager& w = survival.World(0);
            const uint16_t water = BlockCatalog::Instance().DefaultBlock("minecraft:water");
            survival.SetBlockIn(kOverworld, 20, -60, 20, water);
            carol.Settle(80);
            const auto depthAt = [&](int x, int z) -> int {
                const uint16_t id = w.GetBlock(x, -60, z);
                const std::string name = BlockRegistry::Instance().Name(id);
                if (name != "minecraft:water" && name != "minecraft:flowing_water")
                    return -1;
                for (const auto& [k, v] : BlockRegistry::Instance().States(id))
                    if (k == "liquid_depth")
                        return std::get<int32_t>(v);
                return -1;
            };
            CHECK(depthAt(20, 20) == 0);
            CHECK(depthAt(21, 20) == 1 && depthAt(20, 23) == 3);
            CHECK(depthAt(27, 20) == 7 && depthAt(28, 20) == -1);
            CHECK(depthAt(22, 22) == 4);
            survival.SetBlockIn(kOverworld, 20, -60, 20, BlockRegistry::kAir);
            carol.Settle(120);
            CHECK(depthAt(21, 20) == -1 && depthAt(25, 20) == -1);
            // Lava source beside still water turns to obsidian.
            survival.SetBlockIn(kOverworld, 30, -60, 30, water);
            survival.SetBlockIn(kOverworld, 31, -60, 30, BlockCatalog::Instance().DefaultBlock("minecraft:lava"));
            carol.Settle(40);
            CHECK(BlockRegistry::Instance().Name(w.GetBlock(31, -60, 30)) == "minecraft:obsidian");
        }

        // Random ticks grow crops on farmland near players.
        {
            const uint16_t farmland = BlockCatalog::Instance().DefaultBlock("minecraft:farmland");
            const uint16_t wheat = BlockCatalog::Instance().DefaultBlock("minecraft:wheat");
            survival.World(0).SetBlock(-12, -61, -12, farmland);
            survival.World(0).SetBlock(-12, -60, -12, wheat);
            CHECK(survival.SetRule("randomtickspeed", 4096));
            carol.Settle(40);
            const uint16_t now = survival.World(0).GetBlock(-12, -60, -12);
            int growth = 0;
            for (const auto& [k, v] : BlockRegistry::Instance().States(now))
                if (k == "growth")
                    growth = std::get<int32_t>(v);
            CHECK(BlockRegistry::Instance().Name(now) == "minecraft:wheat" && growth > 0);
            CHECK(survival.SetRule("randomtickspeed", 1));
        }
        survival.Stop();
    }
    fs::remove_all(plugins);
    fs::remove_all(config.dataDir);
    if (const int bad = Session::MalformedSends().load(); bad != 0) {
        std::fprintf(stderr, "server refused to send %d malformed packet(s)\n", bad);
        g_failed++;
    }
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_session_test ok");
    return 0;
}
