#pragma once
#include "world/actor/mob/MobDef.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlite {

    class Server;
    class Session;
    struct ChunkPos;

    bool BlockSolid(uint16_t id);
    bool BlockLiquid(uint16_t id);
    bool FallsWithGravity(uint16_t id);
    bool FallingFree(uint16_t id);

    namespace actorkind {
        constexpr int Mob = 0, FallingBlock = 1, PrimedTnt = 2, ExperienceOrb = 3, Projectile = 4;
    }

    namespace projectile {
        constexpr int Arrow = 0, Snowball = 1, Egg = 2, EnderPearl = 3, XpBottle = 4;
    }

    struct Mob {
        uint64_t id = 0;
        const MobDef* def = nullptr;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        float yaw = 0, headYaw = 0, pitch = 0;
        float health = 10;
        int hurtTime = 0;
        int deathTime = 0;
        int attackCooldown = 0;
        int panicTicks = 0;
        int goalTicks = 0;
        double goalX = 0, goalY = 0, goalZ = 0;
        bool hasGoal = false;
        uint64_t target = 0;
        bool angry = false;
        bool onGround = false;
        bool inWater = false;
        bool burning = false;
        bool persistent = false;
        bool baby = false;
        int fuse = -1;
        int age = 0;
        int burnTicks = 0;
        int shootCooldown = 0;
        bool projectile = false;
        int kind = 0;
        uint16_t block = 0;
        uint64_t owner = 0;
        int invulnerable = 0;
        float lastHurt = 0;
        uint64_t lastAttacker = 0;
        int lastAttackerTicks = 0;
        float lastX = 0, lastY = 0, lastZ = 0, lastYaw = 0, lastHead = 0;
        uint64_t lastFlags = 0;
        int size = -1;    // index into the type's size classes (slime, magma cube); -1 when it has none
        int variant = -1; // the "variant" sent to clients (slime size, rabbit coat); -1 sends none
        float maxHealth = 0;  // 0: the type's health
        float damage = -1;    // attack damage for this mob's size; -1: the type's damage
        uint8_t shot = 0;       // projectile:: type, for projectiles
        bool stuck = false;     // an arrow in a block
        bool canPickup = false; // an arrow a survival player can take back
        bool crit = false;      // a fully drawn arrow
        int16_t held = 0;       // item network id in the main hand (skeleton's bow), 0 for none
        float arrowDamage = 2.0f; // an arrow's damage per block/tick of speed (Power raises it)
        int punch = 0;            // Punch level: extra knockback
        bool flame = false;       // sets what it hits on fire
        bool inGrid = false;      // listed in the entity grid, under gridCell
        bool kicked = false;      // pushed by an explosion this tick: the client gets the new motion
        int64_t gridCell = 0;
    };

    class ActorManager {
    public:
        explicit ActorManager(Server& server);

        void Tick();
        Mob* Spawn(const std::string& identifier, double x, double y, double z, bool persistent = true);
        Mob* SpawnFallingBlock(double x, double y, double z, uint16_t block);
        Mob* SpawnPrimedTnt(double x, double y, double z, int fuse);
        // A projectile flying from (x, y, z) at the given velocity (blocks per tick). The owner is the
        // runtime id of the player or mob that shot or threw it.
        Mob* SpawnProjectile(int type, double x, double y, double z, double vx, double vy, double vz, uint64_t owner,
                             bool pickup = true);
        void Explosion(double x, double y, double z, float power, uint64_t source, bool breakBlocks = true);
        bool Kill(uint64_t id);
        // Puts a slime or magma cube in a size class (index into its type's sizes) and redraws it.
        void SetSize(Mob& m, int size);
        bool Remove(uint64_t id);
        bool Damage(uint64_t id, float amount);
        void Moved(Mob& m);
        int KillAll(const std::string& identifier);
        void Show(Session& to) const;
        void Hide(Session& to) const;
        bool Attack(Session& by, uint64_t entity, float damage, float knockback = 0.4f);
        void SpawnXpOrbs(double x, double y, double z, int amount);
        bool Interact(Session& by, uint64_t entity);
        Mob* Find(uint64_t id);
        const std::list<Mob>& All() const { return mobs; }
        size_t Count() const { return mobs.size(); }
        void OnChunkReady(int chunkX, int chunkZ);
        static uint64_t Flags(const Mob& m);

    private:
        void Physics(Mob& m);
        void Think(Mob& m);
        void Broadcast(Mob& m, bool force);
        void Die(Mob& m, Session* killer);
        void NaturalSpawns();
        void SpawnPack(const std::string& category, int x, int z, int surfaceY, bool chunkGen);
        bool Solid(double x, double y, double z) const;
        bool Collides(const Mob& m, double x, double y, double z) const;
        int SurfaceY(int x, int z) const;
        bool Dark(int x, int y, int z) const;
        bool Day() const;
        Session* Nearest(const Mob& m, double range, bool survivalOnly);
        bool Hurt(Mob& m, float damage, double fromX, double fromZ, Session* by, float knockback = 0.4f);
        void Explode(Mob& m);
        bool TickSpecial(Mob& m);
        bool TickFallingBlock(Mob& m);
        bool TickPrimedTnt(Mob& m);
        bool TickExperienceOrb(Mob& m);
        void TickProjectile(Mob& m);
        bool TickShot(Mob& m);
        void Land(Mob& m, double x, double y, double z, Session* shooter);
        void Shoot(Mob& m, Session& at);

        // Adds a mob to the list and the id index; every new entity goes through here.
        Mob& Add(const Mob& m);
        std::list<Mob>::iterator Erase(std::list<Mob>::iterator it);
        // Entities by 8x8 column cell, rebuilt at the start of every tick: explosions and
        // projectiles look only at the cells they reach instead of at every entity.
        void RebuildGrid();
        struct Blast {
            double x = 0, y = 0, z = 0;
            float power = 0;
            uint64_t source = 0;
            bool breakBlocks = true;
            bool fromTnt = false;
            std::string originType;
            uint32_t seed = 0;
            bool coarse = false; // fewer rays: only in ticks with hundreds of explosions
        };
        Blast MakeBlast(double x, double y, double z, float power, uint64_t source, bool breakBlocks);
        void TraceBlast(const Blast& blast, std::vector<std::array<int, 3>>& broken) const;
        void ApplyBlast(const Blast& blast, const std::vector<std::array<int, 3>>& broken);
        void RunBlasts();
        std::vector<Blast> pendingBlasts;
        // During RunBlasts: for each 4x4x4 brick of the box around the tick's blasts, 0 unknown,
        // 1 all air, 2 not.
        struct BrickTable {
            int ox = 0, oy = 0, oz = 0, sx = 0, sy = 0, sz = 0;
            std::unique_ptr<std::atomic<int8_t>[]> states;
            size_t capacity = 0;
            void Reset(const std::vector<Blast>& blasts);
            void Clear() { sx = sy = sz = 0; }
            std::atomic<int8_t>* At(int bx, int by, int bz) const {
                const int ix = bx - ox, iy = by - oy, iz = bz - oz;
                if (ix < 0 || iy < 0 || iz < 0 || ix >= sx || iy >= sy || iz >= sz)
                    return nullptr;
                return &states[(static_cast<size_t>(iy) * static_cast<size_t>(sz) + static_cast<size_t>(iz)) * static_cast<size_t>(sx) +
                               static_cast<size_t>(ix)];
            }
        };
        mutable BrickTable bricks;
        template <class F> void ForNear(double x, double z, double radius, F&& f);

        Server& server;
        std::list<Mob> mobs;
        std::unordered_map<uint64_t, Mob*> byId;
        std::unordered_map<int64_t, std::vector<Mob*>> grid;
        int effectsThisTick = 0;
        int fuseSoundsThisTick = 0;
        std::vector<Session*> tickPlayers; // the overworld's players, taken at the start of a tick
        bool ticking = false;
        std::unordered_set<uint64_t> spawnedChunks;
        std::mt19937 rng{0xC0FFEE};
        uint64_t nextId = 5'000'000;
        int spawnTick = 0;
    };
}
