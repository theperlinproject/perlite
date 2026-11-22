// Load benchmark: simulated players join over the in-process loopback transport, stream their chunks,
// then walk outwards while mobs are active. Reports server tick time percentiles, chunk throughput
// and bytes sent. Not run by ctest; run it by hand:
//
//   server_load_bench [players=4] [radius=8] [walk ticks=400] [mobs per player=20] [generator=vanilla]
//
// The server runs at 20 ticks a second, as in production; tick times are the time Server::Tick takes.
#include "test_auth.hpp"
#include "test_util.hpp"

#include "network/Batch.hpp"
#include "protocol/Packets.hpp"
#include "server/Server.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

using namespace perlite;
namespace proto = perlite::proto2225;
namespace net = perlite::net;

namespace {
    using Clock = std::chrono::steady_clock;

    class Client {
    public:
        Client(Server& server, std::string name)
            : server(server), name(name), endpoint(server.Loopback().Connect()), account(name, "") {}

        template <class P> void Send(const P& packet) {
            std::vector<uint8_t> body;
            net::EncodeChecked(packet, body);
            std::vector<uint8_t> content;
            BatchCodec::AppendPacket(content, P::kId, body);
            const std::vector<uint8_t> wire = codec.Seal(content);
            endpoint->Send(wire.data(), wire.size());
        }

        // Reads everything the server sent; keeps only what the benchmark looks at.
        void Drain() {
            std::vector<uint8_t> wire;
            while (endpoint->Receive(wire)) {
                bytes += wire.size();
                std::vector<uint8_t> content;
                if (!codec.Open(wire, content)) {
                    corrupt = true;
                    continue;
                }
                BatchCodec::Split(content, [&](uint32_t id, std::span<const uint8_t> body) {
                    packets++;
                    if (id == proto::NetworkSettingsPacket::kId)
                        codec.EnableCompression(256);
                    else if (id == proto::LevelChunkPacket::kId)
                        chunks++;
                    else if (id == proto::StartGamePacket::kId) {
                        proto::StartGamePacket p;
                        net::ReadStream s(body.data(), body.size());
                        if (Read(s, p) && s.ok) {
                            x = p.playerPosition.x;
                            y = p.playerPosition.y;
                            z = p.playerPosition.z;
                            started = true;
                        }
                    }
                });
            }
        }

        void Move(uint64_t tick, double dx, double dz) {
            x += dx;
            z += dz;
            proto::PlayerAuthInputPacket in;
            in.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
            in.delta = {static_cast<float>(dx), 0.0f, static_cast<float>(dz)};
            in.tick = tick;
            in.yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / std::numbers::pi);
            Send(in);
        }

        Server& server;
        std::string name;
        std::shared_ptr<LoopbackListener::Endpoint> endpoint;
        testauth::Account account;
        BatchCodec codec;
        bool corrupt = false, started = false;
        uint64_t bytes = 0, packets = 0, chunks = 0;
        double x = 0, y = 0, z = 0;
    };

    struct Stats {
        std::vector<double> ms;
        void Add(double v) { ms.push_back(v); }
        double Pct(double p) const {
            if (ms.empty())
                return 0;
            std::vector<double> s = ms;
            std::sort(s.begin(), s.end());
            const size_t i = std::min(s.size() - 1, static_cast<size_t>(p / 100.0 * static_cast<double>(s.size())));
            return s[i];
        }
        double Mean() const {
            double t = 0;
            for (double v : ms)
                t += v;
            return ms.empty() ? 0 : t / static_cast<double>(ms.size());
        }
        void Print(const char* label) const {
            std::printf("%-8s ticks %5zu  mean %6.2f  p50 %6.2f  p95 %6.2f  p99 %6.2f  max %7.2f ms\n", label,
                        ms.size(), Mean(), Pct(50), Pct(95), Pct(99), Pct(100));
        }
    };
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const int players = argc > 1 ? std::atoi(argv[1]) : 4;
    const int radius = argc > 2 ? std::atoi(argv[2]) : 8;
    const int walkTicks = argc > 3 ? std::atoi(argv[3]) : 400;
    const int mobsPerPlayer = argc > 4 ? std::atoi(argv[4]) : 20;
    const std::string generator = argc > 5 ? argv[5] : "vanilla";

    ServerConfig config;
    config.gameMode = 0;
    config.generator = generator;
    config.loopbackOnly = false;
    config.nethernet = false;
    config.lanDiscovery = false;
    config.terrainBackend = "cpu";
    config.viewDistance = radius;
    config.maxPlayers = std::max(players, 10);
    config.seed = 12345;
    config.dataDir = (fs::temp_directory_path() / "perlite_load_bench").string();
    config.worldStorage = "none";
    config.downloadVanillaPack = false;
    fs::remove_all(config.dataDir);
    fs::create_directories(config.dataDir);
    config.pluginDir = config.dataDir + "/plugins";
    fs::create_directories(config.pluginDir);

    Server server(config);
    std::string error;
    if (!server.Start(error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }

    uint64_t tick = 0;
    Stats all;
    const auto pacedTick = [&](Stats* stats) {
        const auto begin = Clock::now();
        server.Tick();
        const auto end = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - begin).count();
        all.Add(ms);
        if (stats)
            stats->Add(ms);
        tick++;
        const auto next = begin + std::chrono::milliseconds(50);
        if (next > Clock::now())
            std::this_thread::sleep_until(next);
    };

    std::vector<std::unique_ptr<Client>> clients;
    for (int i = 0; i < players; i++)
        clients.push_back(std::make_unique<Client>(server, "Bench" + std::to_string(i)));
    const auto pumpAll = [&](int ticks, Stats* stats) {
        for (int i = 0; i < ticks; i++) {
            pacedTick(stats);
            for (auto& c : clients)
                c->Drain();
        }
    };

    // Join: login, resource packs, chunk radius.
    const auto joinBegin = Clock::now();
    for (auto& c : clients) {
        proto::RequestNetworkSettingsPacket req;
        req.clientProtocol = 2225;
        c->Send(req);
    }
    pumpAll(3, nullptr);
    for (auto& c : clients) {
        proto::LoginPacket login;
        login.protocolVersion = 2225;
        login.tokens.identity = c->account.Identity(c->account.Chain(false), 2);
        login.tokens.client = c->account.ClientToken();
        c->Send(login);
    }
    pumpAll(5, nullptr);
    for (auto& c : clients) {
        proto::ResourcePackClientResponsePacket have;
        have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
        c->Send(have);
    }
    pumpAll(3, nullptr);
    for (auto& c : clients) {
        proto::ResourcePackClientResponsePacket done;
        done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
        c->Send(done);
    }
    pumpAll(3, nullptr);
    for (auto& c : clients) {
        proto::RequestChunkRadiusPacket r;
        r.chunkRadius = radius;
        r.maxRadius = static_cast<uint8_t>(radius);
        c->Send(r);
    }

    // Initial load: until every player has the chunks of its radius (a circle), or 180 s.
    int expected = 0;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dz = -radius; dz <= radius; dz++)
            expected += dx * dx + dz * dz <= radius * radius;
    Stats load;
    const auto loadBegin = Clock::now();
    const auto loaded = [&] {
        for (auto& c : clients)
            if (c->chunks < static_cast<uint64_t>(expected))
                return false;
        return true;
    };
    while (!loaded() && Clock::now() - loadBegin < std::chrono::seconds(180))
        pumpAll(1, &load);
    const double loadSeconds = std::chrono::duration<double>(Clock::now() - loadBegin).count();
    uint64_t loadChunks = 0, loadBytes = 0;
    for (auto& c : clients) {
        loadChunks += c->chunks;
        loadBytes += c->bytes;
        if (!c->started || c->corrupt)
            std::fprintf(stderr, "%s did not join cleanly\n", c->name.c_str());
    }
    std::printf("players %d, radius %d (%d chunks each), generator %s\n", players, radius, expected,
                generator.c_str());
    std::printf("join+load: %.2f s, %llu chunks sent (%.0f chunks/s), %.1f MB\n",
                std::chrono::duration<double>(Clock::now() - joinBegin).count(),
                static_cast<unsigned long long>(loadChunks), static_cast<double>(loadChunks) / loadSeconds,
                static_cast<double>(loadBytes) / 1e6);
    load.Print("load");

    // Walk: every player walks outwards in its own direction at sprint speed (5.6 blocks/s).
    Stats walk;
    uint64_t chunksBefore = 0, bytesBefore = 0;
    for (auto& c : clients) {
        chunksBefore += c->chunks;
        bytesBefore += c->bytes;
    }
    const auto walkBegin = Clock::now();
    for (int t = 0; t < walkTicks; t++) {
        for (size_t i = 0; i < clients.size(); i++) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(clients.size());
            clients[i]->Move(tick, std::cos(angle) * 0.28, std::sin(angle) * 0.28);
        }
        pumpAll(1, &walk);
    }
    const double walkSeconds = std::chrono::duration<double>(Clock::now() - walkBegin).count();
    uint64_t chunksAfter = 0, bytesAfter = 0;
    for (auto& c : clients) {
        chunksAfter += c->chunks;
        bytesAfter += c->bytes;
    }
    std::printf("walk: %.1f s, %llu new chunks (%.0f chunks/s), %.2f MB/s to clients\n", walkSeconds,
                static_cast<unsigned long long>(chunksAfter - chunksBefore),
                static_cast<double>(chunksAfter - chunksBefore) / walkSeconds,
                static_cast<double>(bytesAfter - bytesBefore) / 1e6 / walkSeconds);
    walk.Print("walk");

    // Mobs: summon around each player, then let them run.
    for (auto& c : clients)
        for (int m = 0; m < mobsPerPlayer; m++) {
            const char* kinds[] = {"cow", "sheep", "pig", "chicken", "zombie", "skeleton"};
            const double a = m * 0.7;
            char line[160];
            std::snprintf(line, sizeof line, "summon minecraft:%s %.1f %.1f %.1f", kinds[m % 6],
                          c->x + std::cos(a) * (4 + m % 9), c->y, c->z + std::sin(a) * (4 + m % 9));
            server.RunConsoleCommand(line);
        }
    Stats mobs;
    for (int t = 0; t < walkTicks; t++) {
        for (size_t i = 0; i < clients.size(); i++)
            clients[i]->Move(tick, 0.0, 0.0);
        pumpAll(1, &mobs);
    }
    mobs.Print("mobs");
    all.Print("all");
    server.Stop();
    return 0;
}
