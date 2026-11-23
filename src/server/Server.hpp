#pragma once
#include "server/access/Access.hpp"
#include "server/access/BanList.hpp"
#include "server/commands/Commands.hpp"
#include "server/Config.hpp"
#include "server/packs/ResourcePackManager.hpp"
#include "world/level/storage/WorldStorage.hpp"
#include "network/Connection.hpp"
#include "network/loopback/LoopbackListener.hpp"
#include "plugin/PluginApi.hpp"
#include "plugin/PluginHost.hpp"
#include "network/session/Session.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/actor/ActorManager.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <random>
#include <set>
#include <tuple>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlite {
    class DragonFight;

    struct CatalogItem;

    struct PortalFrame {
        int dimension = 0;
        int x = 0, y = 0, z = 0;
        int axis = 0;
        int width = 2, height = 3;
    };

    struct CommandDef {
        std::string name;
        std::string description;
        std::vector<OverloadSpec> overloads;
        std::string plugin;
        bool operatorOnly = false;
        std::function<void(CommandCtx&)> handler;
    };

    class Server final : public PluginApi {
    public:
        explicit Server(ServerConfig config);
        ~Server() override;

        bool Start(std::string& error);
        void Run();
        void Tick();
        void Stop();
        void RequestStop() { stopRequested = true; }
        bool StopRequested() const { return stopRequested; }

        void RunConsoleCommand(const std::string& line);

        LoopbackListener& Loopback() { return *loopback; }
        // Sessions stop sending chunks for the tick once this passes (each still sends at least one),
        // so a burst of ready chunks is spread over ticks instead of stalling one.
        std::chrono::steady_clock::time_point ChunkSendDeadline() const { return chunkSendDeadline; }

        const ServerConfig& Config() const { return config; }
        const ResourcePackManager& Packs() const { return packs; }
        ChunkManager& Chunks() { return *chunks; }
        ChunkManager& World(int dimension);
        ChunkManager* LoadedWorld(int dimension) const;
        std::vector<Session*> PlayersIn(int dimension);
        void ChangeDimension(Session& who, int dimension, double x, double y, double z);
        uint16_t BlockAt(int dimension, int x, int y, int z);
        void SetBlockIn(int dimension, int x, int y, int z, uint16_t block) { ApplyBlock(dimension, x, y, z, block); }
        PluginHost& Plugins() { return *plugins; }
        uint64_t TickCount() const { return tick; }
        std::chrono::nanoseconds TickDelay() const;
        const std::map<std::string, CommandDef>& Commands() const { return commands; }

        void OnPlayerJoined(Session& session);
        void OnPlayerLeft(Session& session);
        void HandleChat(Session& from, const std::string& message);
        void RunPlayerCommand(Session& from, const std::string& line);
        std::vector<Session*> InGamePlayers();
        void UnloadFarChunks();
        void UpdateChunkViewers();
        std::chrono::steady_clock::time_point chunkSendDeadline{};
        size_t sessionRotation = 0;
        void BroadcastArmor(const Session& wearer);
        Session* FindSession(uint64_t id);
        Session* FindByName(const std::string& name);
        bool IsOperatorName(const std::string& name) const;
        bool IsOperator(const std::string& name, const std::string& xuid) const;
        bool IsWhitelisted(const std::string& name, const std::string& xuid) const {
            return allowList.Contains(name, xuid);
        }
        std::string PermissionOf(const std::string& name, const std::string& xuid) const;
        AccessList& Permissions() { return permissionList; }
        AccessList& Whitelist() { return allowList; }
        std::string AllowlistPath() const;
        std::string PermissionsPath() const;
        bool IgnoresPlayerLimit(const std::string& name, const std::string& xuid) const;
        AccessList& Ops() { return permissionList; }
        std::string SetOperator(const std::string& name, bool op);
        bool Full() const;
        double SpawnX() const { return static_cast<double>(spawnX); }
        double SpawnY() const { return static_cast<double>(spawnY); }
        double SpawnZ() const { return static_cast<double>(spawnZ); }
        void SetWorldSpawn(int x, int y, int z);

        // announce: log Java's "Saving chunks for level ..." lines (stop and save commands).
        // Autosaves only log at debug level.
        void SaveWorld(bool announce = false);
        void EmergencySave();
        // Warns, at most every 15 seconds, when the tick loop has fallen behind.
        void NoteLag(std::chrono::milliseconds behind);
        void LoadPlayer(Session& session);
        void SavePlayer(Session& session);
        bool HasStorage() const { return storage != nullptr; }

        int64_t DayTime() const { return dayTime; }
        void SetDayTime(int64_t ticks);
        void SetWeather(const std::string& kind, int durationTicks);
        bool Raining() const { return raining; }
        bool Thundering() const { return thundering; }
        void SendWorldState(Session& to);
        // Generates (or loads) the chunks around spawn before players arrive, logging progress.
        void PrepareSpawnArea();

        struct GameRule {
            std::string display;
            bool boolean = true;
            int value = 0;
        };
        const std::map<std::string, GameRule>& GameRules() const { return gameRules; }
        int RuleValue(const std::string& lowerName) const;
        bool SetRule(const std::string& lowerName, int value);

        bool IsBanned(const std::string& name, const std::string& xuid) const {
            return banList.Contains(name, xuid);
        }
        AccessList& Bans() { return banList; }
        IpBanList& IpBans() { return ipBans; }

        bool PlayerBreak(Session& by, int x, int y, int z);
        void DropBlockLoot(int dimension, int x, int y, int z, uint16_t block, const std::string& tool,
                           float explosionRadius, Session* by, int fortune = 0, int silkTouch = 0);
        void SpawnXp(int dimension, double x, double y, double z, int amount);
        bool PlayerPlace(Session& by, int x, int y, int z, int face, double clickX = 0.5, double clickY = 0.5,
                         double clickZ = 0.5);
        uint16_t PlacementState(Session& by, uint16_t base, int face, double clickY, int& extraFace);
        void SetLiquidAt(int dimension, int x, int y, int z, uint16_t liquid);
        // Sets fire to an air block that has something to burn or stand on.
        bool LightFire(int dimension, int x, int y, int z);
        // Fills an empty bucket from the source the player looks at, or pours a full one out
        // against the clicked face.
        bool UseBucket(Session& by, const std::string& item, bool clicked, int x, int y, int z, int face);
        void BreakPartner(int dimension, int x, int y, int z, uint16_t broken);
        void SendBlockTo(Session& to, int x, int y, int z);
        void DropFromPlayer(Session& by, const ItemStack& stack);
        void SpawnItem(const ItemStack& stack, double x, double y, double z, double vx, double vy, double vz,
                       int pickupDelay, int dimension = kOverworld);
        const std::vector<ItemEntity>& Items() const { return itemEntities; }
        ActorManager& Mobs() { return *mobWorld; }
        // The entities of a dimension.
        DragonFight& Fight() { return *fight; }
        ActorManager& MobsIn(int dimension) {
            return *mobWorlds[static_cast<size_t>(std::clamp(dimension, 0, kDimensionCount - 1))];
        }
        // The dimension's entity list holding this id, or null.
        ActorManager* ManagerOf(uint64_t entity);
        struct BlockContainer {
            int kind = 0;
            int x = 0, y = 0, z = 0;
            std::vector<ItemStack> slots;
            int burn = 0, burnMax = 0, cook = 0;
            bool lit = false;
            int dimension = 0;
        };
        bool UseBlock(Session& by, int x, int y, int z);
        void SyncContainer(Session& by);
        void CloseContainer(Session& by);
        static int ContainerKindOf(const std::string& blockName);
        const std::vector<uint8_t>& ItemRegistryBody() const { return itemRegistryBody; }
        const std::vector<uint8_t>& CreativeContentBody() const { return creativeBody; }

        void Broadcast(const std::string& text);
        bool Message(uint64_t player, const std::string& text);
        bool Kick(uint64_t player, const std::string& reason);
        bool Teleport(uint64_t player, double x, double y, double z);
        abi::Status SetBlock(int x, int y, int z, const std::string& block);
        abi::Status GetBlock(int x, int y, int z, std::string& block);
        std::expected<int, abi::Status> GiveItem(uint64_t player, const std::string& item, int count);
        void RegisterCommand(const std::string& plugin, const std::string& name,
                             const std::string& description) override;
        void UnregisterCommand(const std::string& plugin, const std::string& name) override;

        abi::host::Result<void> OpBroadcast(PluginCall&, const abi::TextMessage&) override;
        abi::host::Result<void> OpPlayerMessage(PluginCall&, const abi::PlayerText&) override;
        abi::host::Result<void> OpKick(PluginCall&, const abi::PlayerText&) override;
        abi::host::Result<abi::OnlinePlayerList> OpOnlinePlayers(PluginCall&) override;
        abi::host::Result<void> OpTeleport(PluginCall&, const abi::ServerTeleport&) override;
        abi::host::Result<void> OpSetBlock(PluginCall&, const abi::BlockWrite&) override;
        abi::host::Result<abi::TextMessage> OpBlockName(PluginCall&, const abi::BlockCoord&) override;
        abi::host::Result<abi::ServerStatus> OpServerInfo(PluginCall&) override;
        abi::host::Result<abi::GiveItemResult> OpGiveItem(PluginCall&, const abi::GiveItemRequest&) override;
        abi::host::Result<abi::PlayerDetails> OpPlayerInfo(PluginCall&, const abi::PlayerInfoRequest&) override;
        abi::host::Result<void> OpPlayerSetGameMode(PluginCall&, const abi::PlayerSetGameModeRequest&) override;
        abi::host::Result<void> OpPlayerSetHealth(PluginCall&, const abi::PlayerSetHealthRequest&) override;
        abi::host::Result<bool> OpPlayerDamage(PluginCall&, const abi::PlayerDamageRequest&) override;
        abi::host::Result<void> OpPlayerKill(PluginCall&, const abi::PlayerKillRequest&) override;
        abi::host::Result<void> OpPlayerSetFood(PluginCall&, const abi::PlayerSetFoodRequest&) override;
        abi::host::Result<void> OpPlayerAddExperience(PluginCall&, const abi::PlayerAddExperienceRequest&) override;
        abi::host::Result<void> OpPlayerEffectAdd(PluginCall&, const abi::PlayerEffectAddRequest&) override;
        abi::host::Result<void> OpPlayerEffectRemove(PluginCall&, const abi::PlayerEffectRemoveRequest&) override;
        abi::host::Result<abi::ServerInventory> OpPlayerInventory(PluginCall&, const abi::PlayerInventoryRequest&) override;
        abi::host::Result<void> OpPlayerSetSlot(PluginCall&, const abi::PlayerSetSlotRequest&) override;
        abi::host::Result<void> OpPlayerClearInventory(PluginCall&, const abi::PlayerClearInventoryRequest&) override;
        abi::host::Result<void> OpPlayerSetVelocity(PluginCall&, const abi::PlayerSetVelocityRequest&) override;
        abi::host::Result<void> OpPlayerTransferDimension(PluginCall&, const abi::PlayerTransferDimensionRequest&) override;
        abi::host::Result<void> OpPlayerTitle(PluginCall&, const abi::PlayerTitleRequest&) override;
        abi::host::Result<void> OpPlayerActionBar(PluginCall&, const abi::PlayerActionBarRequest&) override;
        abi::host::Result<void> OpPlayerSound(PluginCall&, const abi::PlayerSoundRequest&) override;
        abi::host::Result<void> OpPlayerTransfer(PluginCall&, const abi::PlayerTransferRequest&) override;
        abi::host::Result<void> OpPlayerSetOperator(PluginCall&, const abi::PlayerSetOperatorRequest&) override;
        abi::host::Result<uint32_t> OpPlayerForm(PluginCall&, const abi::PlayerFormRequest&) override;
        abi::host::Result<void> OpPlayerRunCommand(PluginCall&, const abi::PlayerRunCommandRequest&) override;
        abi::host::Result<void> OpPlayerSetSpawn(PluginCall&, const abi::PlayerSetSpawnRequest&) override;
        abi::host::Result<uint64_t> OpPlayerByName(PluginCall&, const abi::PlayerByNameRequest&) override;
        abi::host::Result<abi::BlockInfo> OpBlockState(PluginCall&, const abi::BlockStateRequest&) override;
        abi::host::Result<void> OpBlockStateSet(PluginCall&, const abi::BlockStateSetRequest&) override;
        abi::host::Result<uint32_t> OpFill(PluginCall&, const abi::FillRequest&) override;
        abi::host::Result<abi::TimeGetResponse> OpTimeGet(PluginCall&) override;
        abi::host::Result<void> OpTimeSet(PluginCall&, const abi::TimeSetRequest&) override;
        abi::host::Result<void> OpWeatherSet(PluginCall&, const abi::WeatherSetRequest&) override;
        abi::host::Result<int32_t> OpGameRuleGet(PluginCall&, const abi::GameRuleGetRequest&) override;
        abi::host::Result<void> OpGameRuleSet(PluginCall&, const abi::GameRuleSetRequest&) override;
        abi::host::Result<void> OpDifficultySet(PluginCall&, const abi::DifficultySetRequest&) override;
        abi::host::Result<void> OpExplode(PluginCall&, const abi::ExplodeRequest&) override;
        abi::host::Result<int32_t> OpHighestBlock(PluginCall&, const abi::HighestBlockRequest&) override;
        abi::host::Result<void> OpParticleAt(PluginCall&, const abi::ParticleAtRequest&) override;
        abi::host::Result<void> OpDropItem(PluginCall&, const abi::DropItemRequest&) override;
        abi::host::Result<abi::BlockCoord> OpWorldSpawnGet(PluginCall&) override;
        abi::host::Result<void> OpWorldSpawnSet(PluginCall&, const abi::BlockCoord&) override;
        abi::host::Result<uint64_t> OpEntitySpawn(PluginCall&, const abi::EntitySpawnRequest&) override;
        abi::host::Result<abi::EntityListResponse> OpEntityList(PluginCall&, const abi::EntityListRequest&) override;
        abi::host::Result<void> OpEntityRemove(PluginCall&, const abi::EntityRemoveRequest&) override;
        abi::host::Result<bool> OpEntityDamage(PluginCall&, const abi::EntityDamageRequest&) override;
        abi::host::Result<void> OpEntityTeleport(PluginCall&, const abi::EntityTeleportRequest&) override;
        abi::host::Result<void> OpEntitySetVelocity(PluginCall&, const abi::EntitySetVelocityRequest&) override;
        abi::host::Result<abi::ServerEntity> OpEntityInfo(PluginCall&, const abi::EntityInfoRequest&) override;
        abi::host::Result<void> OpEntitySetHealth(PluginCall&, const abi::EntitySetHealthRequest&) override;
        abi::host::Result<abi::RestText> OpRunCommand(PluginCall&, const abi::RunCommandRequest&) override;
        abi::host::Result<void> OpMotdSet(PluginCall&, const abi::MotdSetRequest&) override;
        abi::host::Result<void> OpLootTableOverride(PluginCall&, const abi::LootTableOverrideRequest&) override;
        abi::host::Result<abi::RestText> OpLootTableGet(PluginCall&, const abi::LootTableGetRequest&) override;
#if PERLITE_PERLIN_REV3
        abi::host::Result<void> OpContentEntityRegister(PluginCall&, const abi::ContentEntity&) override;
        abi::host::Result<void> OpContentItemRegister(PluginCall&, const abi::ContentItem&) override;
        abi::host::Result<void> OpContentBlockRegister(PluginCall&, const abi::ContentBlock&) override;
        abi::host::Result<void> OpContentBiomeRegister(PluginCall&, const abi::ContentBiome&) override;
        abi::host::Result<void> OpContentUnregister(PluginCall&, const abi::ContentUnregisterRequest&) override;
        abi::host::Result<abi::ContentListResponse> OpContentList(PluginCall&) override;
        abi::host::Result<void> OpModChannelSend(PluginCall&, const abi::ModChannelMessage&) override;
#endif
        // A client mod's message on a mod channel; the content list goes to a client mod saying hello.
        void HookModChannel(Session& from, const std::string& channel, const std::string& body);
        // Content mods' biomes and spawns on a new chunk, then the ChunkGenerated event.
        void ContentOnNewChunk(int dimension, ChunkPos pos, ChunkColumn& column);
        void TickContentSpawns();
        void EnsureContent();
        void AnnounceContent();

        bool HookPreLogin(Session& who, std::string& reason);
        bool HookPlayerDamage(Session& who, float& amount, Session::DamageCause cause, Session* attacker,
                              const std::string& source);
        void HookPlayerDeath(Session& who, Session::DamageCause cause, const std::string& source);
        void HookPlayerRespawn(Session& who);
        bool HookPlayerUse(Session& who, int x, int y, int z, int face);
        bool HookPlayerAttack(Session& who, uint64_t target, float& damage);
        bool HookEntityDamage(Mob& mob, float& amount, Session* attacker);
        void HookEntityDeath(Mob& mob, Session* killer);
        bool HookEntitySpawn(const std::string& type, uint64_t id, double x, double y, double z, bool natural);
        bool HookItemConsume(Session& who, const std::string& item);
        void HookGameModeChange(Session& who, int from, int to);
        void HookFormResponse(Session& who, uint32_t form, const std::string& data);
        void HookPlayerMove(Session& who);
        bool HookExplosion(double x, double y, double z, float& power, uint64_t source);
        bool HookItemDrop(Session& who, const ItemStack& stack);
        bool HookItemPickup(Session& who, const ItemStack& stack);
        bool HookContainerOpen(Session& who, int x, int y, int z, const std::string& block);
        void HookDimensionChange(Session& who, int from, int to);
        bool HookServerCommand(Session* from, std::string& line);

        std::string Describe() const;

    private:
        struct CommandRegistrar;
        void RegisterBuiltins();
        void RegisterInfoCommands(const CommandRegistrar& r);
        void RegisterPlayerCommands(const CommandRegistrar& r);
        void RegisterAccessCommands(const CommandRegistrar& r);
        void RegisterWorldCommands(const CommandRegistrar& r);
        void RegisterServerCommands(const CommandRegistrar& r);
        void RegisterExtraCommands(const CommandRegistrar& r);
        double TicksPerSecond() const;
        void BuildItemPackets();
        void ApplyBlock(int x, int y, int z, uint16_t block) { ApplyBlock(kOverworld, x, y, z, block); }
        void ApplyBlock(int dimension, int x, int y, int z, uint16_t block);
        void Pump();
        void TickItems();
        void ReapClosed();

        ServerConfig config;
        std::unique_ptr<ChunkManager> chunks;
        int unloadCountdown = 100;
        std::array<std::unique_ptr<ChunkManager>, kDimensionCount> otherWorlds;
        std::unique_ptr<PluginHost> plugins;
        std::unique_ptr<LoopbackListener> loopback;
        std::unique_ptr<Listener> nethernet;
        EventQueue events;
        int spawnX = 0, spawnY = 0, spawnZ = 0;
        int64_t dayTime = 6000;
        bool raining = false, thundering = false;
        int weatherTicks = 0;
        std::map<std::string, GameRule> gameRules;
        AccessList banList{AccessList::Kind::Allowlist};
        ResourcePackManager packs;
        IpBanList ipBans;
        bool savingHeld = false;
        std::deque<std::chrono::steady_clock::time_point> tickTimes;
        void InitGameRules();
        void TickWeather();
        void TickSleep();
        std::mt19937 worldRng{std::random_device{}()};
        int sleepTicks = 0;
        bool OpenWorld(std::string& error);
        LevelInfo CurrentLevelInfo() const;
        void AutosaveTick();
        void CloseWorld();
        std::unique_ptr<WorldStorage> storage;
        std::chrono::steady_clock::time_point lastSave = std::chrono::steady_clock::now();
        bool levelLoaded = false;
        void DrainLocate();
        std::mutex locateMutex;
        std::vector<std::pair<uint64_t, std::string>> locateDone;
        std::vector<std::thread> locateThreads;
        std::set<uint64_t> locating;
        std::atomic<bool> locateCancel{false};
        // Read by the signalling and LAN discovery threads, written once per tick.
        std::atomic<int> onlineCount{0};
        AccessList permissionList{AccessList::Kind::Permissions}, allowList{AccessList::Kind::Allowlist};

        std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions;
        uint64_t nextRuntimeId = 1;
        std::map<std::string, CommandDef> commands;
        std::vector<uint8_t> itemRegistryBody, creativeBody;
        std::vector<ItemEntity> itemEntities;
        std::array<std::unique_ptr<ActorManager>, kDimensionCount> mobWorlds;
        std::unique_ptr<DragonFight> fight;
        struct ContentState;
        std::shared_ptr<ContentState> content; // shared: destroyed where the type is known
        ActorManager* mobWorld = nullptr; // the overworld's
        std::unordered_map<int64_t, BlockContainer> containers;
        void TickContainers();
        void SaveContainers();
        void LoadContainers(int dimension, ChunkPos pos);
        void ConsumeGenerated(int dimension, ChunkPos pos);
        std::set<std::tuple<int, int, int>> blockEntityChunks;
        void DropContainer(int dimension, int x, int y, int z);
        bool UseItemOn(Session& by, int dimension, int x, int y, int z, int face, const CatalogItem& item);
        void OnBlockRemoved(Session& by, int dimension, int x, int y, int z, const std::string& name);
        bool TryLightPortal(int dimension, int x, int y, int z);
        bool FindPortalFrame(int dimension, int x, int y, int z, int axis, PortalFrame& out);
        void FillPortal(const PortalFrame& frame);
        bool PlacePortalNear(int dimension, int x, int y, int z, PortalFrame& out);
        // A lit nether portal already in the world near (x, z), found by looking at its blocks.
        bool FindPortalNear(int dimension, int x, int y, int z, int reach, PortalFrame& out);
        void LoadPortals();
        void SavePortals();
        std::vector<PortalFrame> portals;
        void TickPortals();
        void TickGravity();
        void TickFire();
        void TickFluids();
        void QueueBlockUpdate(int dimension, int x, int y, int z, uint16_t block, int layer);
        void FlushBlockUpdates();
        void FlowAt(int dimension, int x, int y, int z);
        int FluidDelay(int dimension, uint16_t block) const;
        void ScheduleFluid(int dimension, int x, int y, int z, int delay);
        void FluidNeighbourChanged(int dimension, int x, int y, int z);
        void ScheduleFire(int dimension, int x, int y, int z);
        // Random block ticks around players: crops, saplings, grass, leaves and the rest of the
        // blocks that change by themselves over time.
        void TickRandom();
        void RandomTickBlock(int dimension, int x, int y, int z, std::mt19937& rng);
        bool GrowTree(int dimension, int x, int y, int z, const std::string& kind, std::mt19937& rng);
        std::vector<std::array<int, 4>> gravityQueue;
        struct FireTick {
            int dimension, x, y, z, due;
        };
        std::vector<FireTick> fireQueue;
        struct FluidTick {
            int dimension, x, y, z, due;
        };
        std::vector<FluidTick> fluidQueue;
        struct PendingChunkBlocks {
            int dimension = 0;
            ChunkPos chunk{};
            std::map<std::array<int, 4>, uint16_t> changes; // x y z layer -> block
        };
        std::unordered_map<uint64_t, PendingChunkBlocks> pendingBlocks;
        std::unordered_set<uint64_t> fluidPending;
        void TravelThroughPortal(Session& who, bool endPortal);
        bool PortalTargetReady(Session& who, bool endPortal);
        void BuildEndPlatform();
        bool endPlatformBuilt = false;
        void PushContainer(BlockContainer& c, bool slotsChanged);
        uint64_t nextEntityId = 1'000'000;
        uint64_t tick = 0;
        double tickRateTarget = 20.0;
        bool frozen = false;
        int64_t stepRemaining = 0;
        int64_t sprintRemaining = 0;
        int64_t sprintTotal = 0;
        std::chrono::steady_clock::time_point sprintStarted;
        std::deque<double> tickMillis;
        std::string TickQuery() const;
        void RunTickCommand(Session* from, const std::string& args);
        void RunCommand(Session* from, const std::string& line);
        int RunExecute(Session* from, const std::string& args);
        bool executeElevated = false;
        std::atomic<bool> stopRequested{false};
        std::map<std::pair<uint64_t, uint32_t>, abi::FormKind> formKinds;
        bool started = false;
    };

    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig& config,
                                                      std::function<std::string()> infoJson);
}
