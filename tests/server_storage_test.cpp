#include <cmath>
#include "test_auth.hpp"
#include "test_util.hpp"

#include "network/Batch.hpp"
#include "server/Server.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/storage/WorldStorage.hpp"

#include "protocol/Packets.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace fs = std::filesystem;

namespace {
    class Client {
    public:
        Client(Server& server, const std::string& name)
            : server(server), endpoint(server.Loopback().Connect()), account(name, "") {}

        template <class P> void Send(const P& packet) {
            std::vector<uint8_t> body;
            net::EncodeChecked(packet, body);
            std::vector<uint8_t> content;
            BatchCodec::AppendPacket(content, P::kId, body);
            const std::vector<uint8_t> wire = codec.Seal(content);
            endpoint->Send(wire.data(), wire.size());
        }

        void Settle(int ticks = 4) {
            for (int i = 0; i < ticks; i++) {
                server.Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            std::vector<uint8_t> wire;
            while (endpoint->Receive(wire)) {
                std::vector<uint8_t> content;
                if (!codec.Open(wire, content))
                    continue;
                BatchCodec::Split(content, [&](uint32_t id, std::span<const uint8_t> body) {
                    if (id == proto::NetworkSettingsPacket::kId)
                        codec.EnableCompression(1);
                    if (id == proto::StartGamePacket::kId) {
                        proto::StartGamePacket g;
                        net::ReadStream s(body.data(), body.size());
                        if (Read(s, g) && s.ok)
                            start = g;
                    }
                });
            }
        }

        void Join() {
            proto::RequestNetworkSettingsPacket req;
            req.clientProtocol = 2225;
            Send(req);
            Settle();
            proto::LoginPacket login;
            login.protocolVersion = 2225;
            login.tokens.identity = account.Identity(account.Chain(false), 2);
            login.tokens.client = account.ClientToken();
            Send(login);
            Settle();
            proto::ResourcePackClientResponsePacket have;
            have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
            Send(have);
            Settle();
            proto::ResourcePackClientResponsePacket done;
            done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
            Send(done);
            Settle();
            proto::RequestChunkRadiusPacket radius;
            radius.chunkRadius = 2;
            radius.maxRadius = 2;
            Send(radius);
            Settle(20);
            Send(proto::SetLocalPlayerAsInitializedPacket{});
            Settle();
        }

        void Leave() {
            endpoint->Close();
            Settle();
        }

        Server& server;
        std::shared_ptr<LoopbackListener::Endpoint> endpoint;
        testauth::Account account;
        BatchCodec codec;
        std::optional<proto::StartGamePacket> start;
    };

    ServerConfig Config(const fs::path& dir) {
        ServerConfig config;
        config.dataDir = dir.string();
        config.levelName = "Storage Test";
        config.generator = "flat";
        config.terrainBackend = "cpu";
        config.loopbackOnly = true;
        config.worldStorage = "leveldb";
        config.seed = 424242;
        config.randomSeed = false;
        config.gameMode = 0;
        return config;
    }

    void RoundTripSections() {
        BlockRegistry& registry = BlockRegistry::Instance();
        const uint16_t stone = registry.Intern("minecraft:stone");
        const uint16_t log = registry.Intern("minecraft:oak_log", {{"pillar_axis", std::string("x")}});
        const uint16_t slab = registry.Intern(
            "minecraft:oak_slab", {{"minecraft:vertical_half", std::string("top")}, {"top_slot_bit", true}});
        ChunkColumn column;
        column.FillSection(4, stone);
        column.Set(3, 0, 7, log);
        column.Set(15, 15, 15, slab);
        column.Set(0, -64, 0, stone);
        std::string encoded;
        WorldStorage::EncodeSubChunk(column, 4, 0, encoded);
        std::vector<uint16_t> cells(kCells);
        uint16_t uniform = 0;
        bool isUniform = false;
        CHECK(WorldStorage::DecodeSubChunk(encoded, cells.data(), uniform, isUniform));
        CHECK(!isUniform);
        CHECK(cells[static_cast<size_t>(CellIndex(3, 0, 7))] == log);
        CHECK(cells[static_cast<size_t>(CellIndex(15, 15, 15))] == slab);
        CHECK(cells[static_cast<size_t>(CellIndex(1, 1, 1))] == stone);
        WorldStorage::EncodeSubChunk(column, 5, 1, encoded);
        CHECK(WorldStorage::DecodeSubChunk(encoded, cells.data(), uniform, isUniform));
        CHECK(isUniform && uniform == BlockRegistry::kAir);
        CHECK(encoded[0] == 9 && encoded[2] == 1);

        LevelInfo info;
        info.name = "Round Trip";
        info.seed = 0xFEEDFACECAFEBEEFull;
        info.spawnX = -12;
        info.spawnY = 70;
        info.spawnZ = 99;
        info.time = 13000;
        info.boolRules = {{"dodaylightcycle", false}};
        info.intRules = {{"randomtickspeed", 7}};
        LevelInfo back;
        CHECK(WorldStorage::ParseLevelDat(WorldStorage::LevelDat(info), back));
        CHECK(back.name == info.name && back.seed == info.seed && back.spawnX == -12 && back.spawnZ == 99 &&
              back.time == 13000);
        bool cycle = true;
        for (const auto& [k, v] : back.boolRules)
            if (k == "dodaylightcycle")
                cycle = v;
        CHECK(!cycle);
    }
}

// A nether chunk keeps its 3D biomes through the disk (it was coming back as plains, with overworld fog).
static void NetherBiomes(const fs::path& dir) {
    WorldStorage storage;
    std::string error;
    CHECK(storage.Open((dir / "biomes").string(), error));
    ChunkColumn c;
    c.FillSection(RangeOf(kNether).FirstSection(), BlockCatalog::Instance().DefaultBlock("minecraft:netherrack"));
    std::vector<uint8_t> table(static_cast<size_t>(16 * ChunkColumn::kQuartLayers), 0), net(table.size(), 8);
    net[static_cast<size_t>(ChunkColumn::QuartIndex(1, RangeOf(kNether).FirstSection() * 4 + 9, 2))] = 178;
    c.SetQuartBiomes(table.data(), net.data());
    storage.SaveChunk(kNether, {3, 4}, c);
    CHECK(storage.Flush());
    ChunkColumn back;
    CHECK(storage.LoadChunk(kNether, {3, 4}, back));
    const int base = RangeOf(kNether).FirstSection() * 4;
    CHECK(back.QuartNet(0, base, 0) == 8 && back.QuartNet(3, base + 20, 3) == 8);
    CHECK(back.QuartNet(1, base + 9, 2) == 178);
    CHECK(back.NetBiome(5, 5) == 8);
    storage.Close();
}

int main() {
    RoundTripSections();

    const fs::path dir = fs::temp_directory_path() / "perlite_storage_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    NetherBiomes(dir);

    uint16_t gold = 0;
    {
        Server server(Config(dir));
        std::string error;
        CHECK(server.Start(error));
        CHECK(server.HasStorage());
        gold = BlockCatalog::Instance().DefaultBlock("minecraft:gold_block");
        CHECK(gold != BlockRegistry::kAir);
        server.Chunks().Load({0, 0});
        server.Chunks().Load({-3, 5});
        server.SetBlockIn(kOverworld, 1, -50, 2, gold);
        server.SetBlockIn(kOverworld, -40, -45, 85, gold);
        server.SetDayTime(17000);
        Client steve(server, "Steve");
        steve.Join();
        Session* s = server.FindByName("Steve");
        CHECK(s && s->InGame());
        if (s) {
            s->Teleport(10.5, -40.0 + 1.62, 20.5);
            const CatalogItem* diamond = BlockCatalog::Instance().ItemByName("minecraft:diamond");
            CHECK(diamond != nullptr);
            if (diamond)
                s->Inventory().Give(diamond->networkId, 5);
            const uint16_t chest = BlockCatalog::Instance().DefaultBlock("minecraft:chest");
            server.SetBlockIn(kOverworld, 11, -40, 21, chest);
            CHECK(server.UseBlock(*s, 11, -40, 21));
            if (diamond && s->Inventory().container.size() == 27) {
                s->Inventory().container[4] = s->Inventory().MakeStack(diamond->networkId, 7);
                server.SyncContainer(*s);
            }
            server.CloseContainer(*s);
        }
        steve.Leave();
        server.Stop();
    }
    CHECK(fs::exists(dir / "worlds" / "Storage Test" / "level.dat"));
    CHECK(fs::exists(dir / "worlds" / "Storage Test" / "db"));
    {
        ServerConfig again = Config(dir);
        again.seed = 1;
        Server server(again);
        std::string error;
        CHECK(server.Start(error));
        CHECK(server.Config().seed == 424242);
        CHECK(server.DayTime() >= 17000 && server.DayTime() < 17300);
        server.Chunks().Load({0, 0});
        server.Chunks().Load({-3, 5});
        CHECK(server.Chunks().GetBlock(1, -50, 2) == gold);
        CHECK(server.Chunks().GetBlock(-40, -45, 85) == gold);
        Client steve(server, "Steve");
        steve.Join();
        Session* s = server.FindByName("Steve");
        CHECK(s && s->InGame());
        if (s) {
            CHECK(std::abs(s->X() - 10.5) < 0.01 && std::abs(s->Z() - 20.5) < 0.01);
            const CatalogItem* diamond = BlockCatalog::Instance().ItemByName("minecraft:diamond");
            int diamonds = 0;
            for (const ItemStack& stack : s->Inventory().main)
                if (diamond && stack.networkId == diamond->networkId)
                    diamonds += stack.count;
            CHECK(diamonds == 5);
            server.Tick();
            CHECK(server.UseBlock(*s, 11, -40, 21));
            CHECK(s->Inventory().container.size() == 27 && diamond &&
                  s->Inventory().container[4].networkId == diamond->networkId &&
                  s->Inventory().container[4].count == 7);
            server.CloseContainer(*s);
        }
        CHECK(steve.start && std::abs(steve.start->playerPosition.x - 10.5f) < 0.01f);
        steve.Leave();
        server.Stop();
    }
    {
        // A world keeps what it was made with: reopened by a host that asks for the default generator and other
        // settings, it is still flat, and its in-process game mode, difficulty and cheats are the saved ones.
        ServerConfig again = Config(dir);
        again.generator = "vanilla";
        again.gameMode = 1;
        again.difficulty = 3;
        again.allowCheats = true;
        again.generateStructures = true;
        Server server(again);
        std::string error;
        CHECK(server.Start(error));
        CHECK(server.Config().generator == "flat");
        CHECK(server.Config().gameMode == 0);
        CHECK(server.Config().difficulty == 1);
        CHECK(!server.Config().allowCheats);
        server.Stop();
    }
    {
        // A world folder apart from its name, with the settings of a creation screen and a bonus chest.
        ServerConfig made = Config(dir);
        made.levelName = "My: World";
        made.worldFolder = "My_ World (1)";
        made.difficulty = 3;
        made.allowCheats = true;
        made.generateStructures = false;
        made.bonusChest = true;
        made.seed = 777;
        Server server(made);
        std::string error;
        CHECK(server.Start(error));
        const int sx = static_cast<int>(std::floor(server.SpawnX())), sz = static_cast<int>(std::floor(server.SpawnZ()));
        server.Chunks().Load({sx >> 4, sz >> 4});
        server.Tick();
        const uint16_t chest = BlockCatalog::Instance().DefaultBlock("minecraft:chest");
        int chests = 0;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -64; y < 320; y++)
                    if (server.Chunks().GetBlock((sx & ~15) + x, y, (sz & ~15) + z) == chest)
                        chests++;
        CHECK(chests == 1);
        server.Stop();
        CHECK(fs::exists(dir / "worlds" / "My_ World (1)" / "level.dat"));
        ServerConfig plain = Config(dir);
        plain.worldFolder = "My_ World (1)";
        Server reopened(plain);
        CHECK(reopened.Start(error));
        CHECK(reopened.Config().levelName == "My: World");
        CHECK(reopened.Config().difficulty == 3 && reopened.Config().allowCheats);
        CHECK(!reopened.Config().generateStructures && !reopened.Config().bonusChest);
        reopened.Stop();
    }
    fs::remove_all(dir, ec);
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_storage_test ok");
    return 0;
}
