// Everyday play: a placed block is used up and stays used up, a plant goes with the block under it, and a
// wooden door opens and closes by hand.
#include "test_util.hpp"

#include "support/LoopbackClient.hpp"

#include "network/session/Session.hpp"
#include "world/level/block/BlockCatalog.hpp"

#include <filesystem>

using namespace perlite;
using namespace perlite::testclient;

int main() {
    namespace fs = std::filesystem;
    ServerConfig config;
    config.gameMode = 0;
    config.generator = "flat";
    config.loopbackOnly = false;
    config.nethernet = false;
    config.viewDistance = 4;
    config.dataDir = (fs::temp_directory_path() / "perlite_inventory_sync").string();
    config.worldStorage = "none";
    fs::create_directories(config.dataDir);
    Server server(config);
    std::string error;
    CHECK(server.Start(error));
    Client alice(server, "Alice");
    Join(alice);
    proto::SetLocalPlayerAsInitializedPacket init;
    alice.Send(init);
    alice.Settle(10);
    Session* a = server.FindByName("Alice");
    CHECK(a != nullptr);
    if (!a)
        return 1;
    const CatalogItem* dirt = BlockCatalog::Instance().ItemByName("minecraft:dirt");
    const CatalogItem* bread = BlockCatalog::Instance().ItemByName("minecraft:bread");
    CHECK(dirt && bread);
    a->Inventory().main[0] = a->Inventory().MakeStack(dirt->networkId, 5);
    a->Inventory().main[1] = a->Inventory().MakeStack(bread->networkId, 3);
    const int surface = static_cast<int>(std::floor(a->Y() - 1.62)) - 1;

    proto::InventoryTransactionPacket place;
    place.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
    proto::TransactionUseItem use;
    use.actionType = proto::TransactionUseItemActionType::ClickBlock;
    use.triggerType = proto::TransactionUseItemTriggerType::PlayerInput;
    use.blockPosition = {static_cast<int>(a->X()) + 2, surface, static_cast<int>(a->Z())};
    use.face = 1;
    use.hotbarSlot = 0;
    place.transaction.transactionData = use;
    alice.Send(place);
    alice.Settle(2);
    std::printf("after place: dirt %d\n", a->Inventory().main[0].count);
    CHECK(a->Inventory().main[0].count == 4);

    proto::InventoryTransactionPacket eat;
    eat.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
    proto::TransactionUseItem air;
    air.actionType = proto::TransactionUseItemActionType::ClickAir;
    air.triggerType = proto::TransactionUseItemTriggerType::PlayerInput;
    air.hotbarSlot = 1;
    eat.transaction.transactionData = air;
    alice.Send(eat);
    alice.Settle(40);
    proto::InventoryTransactionPacket done;
    done.transaction.transactionType = proto::TransactionTransactionType::ItemRelease;
    proto::TransactionTransactionDataCase2 release;
    release.actionType = proto::TransactionTransactionDataCase2ActionType::Consume;
    release.hotbarSlot = 1;
    done.transaction.transactionData = release;
    alice.Send(done);
    alice.Settle(4);
    std::printf("after eating: dirt %d bread %d\n", a->Inventory().main[0].count, a->Inventory().main[1].count);
    CHECK(a->Inventory().main[0].count == 4);
    // A plant goes when the block under it does; a door opens by hand, both halves together.
    {
        const int gx = static_cast<int>(a->X()) + 4, gz = static_cast<int>(a->Z());
        server.SetBlock(gx, surface + 1, gz, "minecraft:short_grass");
        alice.Settle(2);
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, gx, surface + 1, gz)) == "minecraft:short_grass");
        server.SetBlock(gx, surface, gz, "minecraft:air");
        alice.Settle(3);
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, gx, surface + 1, gz)) == "minecraft:air");

        const int dx = static_cast<int>(a->X()) + 1, dz = static_cast<int>(a->Z()) + 2;
        server.SetBlock(dx, surface + 1, dz, "minecraft:air");
        alice.Settle(1);
        const CatalogItem* door = BlockCatalog::Instance().ItemByName("minecraft:wooden_door");
        CHECK(door != nullptr);
        if (door) {
            a->Inventory().main[2] = a->Inventory().MakeStack(door->networkId, 1);
            a->Inventory().selected = 2;
            CHECK(server.PlayerPlace(*a, dx, surface, dz, 1, 0.5, 1.0, 0.5));
        }
        alice.Settle(2);
        CHECK(server.UseBlock(*a, dx, surface + 2, dz));
        alice.Settle(2);
        const auto openBit = [&](int y) {
            for (const auto& [k, v] : BlockRegistry::Instance().States(server.BlockAt(0, dx, y, dz)))
                if (k == "open_bit")
                    return std::get<bool>(v);
            return false;
        };
        CHECK(openBit(surface + 1) && openBit(surface + 2));
        CHECK(BlockRegistry::Instance().Name(server.BlockAt(0, dx, surface + 2, dz)) == "minecraft:wooden_door");
        CHECK(server.UseBlock(*a, dx, surface + 1, dz));
        alice.Settle(2);
        CHECK(!openBit(surface + 1) && !openBit(surface + 2));
    }
    server.Stop();
    if (g_failed)
        return 1;
    std::printf("server_inventory_sync_test: ok\n");
    return 0;
}
