#pragma once
#include "world/actor/mob/MobDef.hpp"

#include <cstdint>
#include <list>
#include <random>
#include <string>
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

        Server& server;
        std::list<Mob> mobs;
        std::unordered_set<uint64_t> spawnedChunks;
        std::mt19937 rng{0xC0FFEE};
        uint64_t nextId = 5'000'000;
        int spawnTick = 0;
    };
}
