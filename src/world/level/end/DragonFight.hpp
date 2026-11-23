#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace perlite {
    class Server;
    class Session;
    struct Mob;

    // The fight with the ender dragon on the End's main island, as the game runs it: the exit
    // portal podium at the centre, the end crystals on the ten obsidian pillars, the dragon and its
    // boss bar, the end gateway that opens after each kill, and the respawn with four crystals on the
    // exit portal. Kept in dragon.txt in the world folder.
    class DragonFight {
    public:
        explicit DragonFight(Server& server) : server(server) {}

        void Load(const std::string& directory);
        void Save() const;
        void Tick();
        // An End chunk is ready: the crystals of the pillars in it appear.
        void OnChunkReady(int chunkX, int chunkZ);

        void CrystalDestroyed(uint64_t id);
        // The dragon's death animation finished.
        void DragonKilled(uint64_t id);
        // A player uses an end crystal on a block; true when it was placed.
        bool UseEndCrystal(Session& by, int x, int y, int z);
        // A player stepped into an end gateway.
        void EnterGateway(Session& who, int x, int y, int z);
        // A player stepped into the exit portal: credits the first time.
        bool FirstExit(Session& who);

        int CrystalsAlive() const;
        // Chunks the fight keeps loaded while players are in the End: the main island (as the
        // game's dragon ticket does), and where gateway trips are going. {chunk x, chunk z, radius}.
        std::vector<std::array<int, 3>> Anchors() const;
        uint64_t Dragon() const { return dragon; }
        bool Killed() const { return killed; }
        bool PreviouslyKilled() const { return previouslyKilled; }
        int PortalY() const { return portalY; }
        // The dragon's flight path: 24 nodes on three rings around the island (12 outer, 8 middle,
        // 4 inner), each above the ground below it.
        std::array<double, 3> Node(int index);
        int NearestNode(double x, double y, double z);
        // The crystals around the island, for healing: {id, x, y, z}.
        std::vector<uint64_t> PillarCrystals() const;

    private:
        void PlacePodium(bool active);
        void SpawnGateway();
        void PlaceGateway(int x, int y, int z);
        void UpdateBossBar();
        void HideBossBar(Session& who);
        void TickRespawn();
        void TickGatewayTrips();
        void SendThrough(Session& who, const std::array<int, 3>& gate, const std::array<int, 3>& to);
        int Top(int x, int z) const;
        bool ChunksReady(int x0, int z0, int x1, int z1);

        Server& server;
        std::string directory;
        bool loaded = false;
        bool killed = false, previouslyKilled = false;
        int portalY = -1;
        bool podiumActive = false;
        uint64_t dragon = 0;
        int gatewaysOpened = 0;
        std::vector<bool> crystalGone;          // per pillar: its crystal was destroyed
        std::map<int, uint64_t> pillarCrystal;  // pillar -> crystal entity
        std::map<std::array<int, 3>, std::array<int, 3>> gatewayExits; // gateway -> where it sends players
        std::set<std::string> creditsSeen;
        std::set<uint64_t> barShown;
        float barHealth = -1;
        int tickCount = 0;
        std::vector<std::array<double, 3>> nodes;

        // The respawn: -1 when not running, otherwise the stage (0 start, 1 preparing, 2 pillars,
        // 3 dragon) and ticks in it, with the four crystals that started it.
        int respawnStage = -1, respawnTicks = 0;
        std::vector<uint64_t> respawnCrystals;

        // Gateway trips waiting for the chunks around the outer exit to generate.
        struct Trip {
            uint64_t player = 0;
            std::array<int, 3> gateway{};
            int stage = 0, ticks = 0;
            double ex = 0, ez = 0;      // the chosen exit column
            std::array<int, 3> island{}; // where the player lands
        };
        std::vector<Trip> trips;
    };
}
