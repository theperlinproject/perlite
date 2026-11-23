// The ender dragon fight. The rules follow the game's: the podium is built where the island's
// surface is at (0, 0), the dragon appears above it the first time a player is in the End, a
// gateway opens and the exit portal lights when it dies (with the egg on top the first time), and
// four crystals on the exit portal's rim start a respawn that rebuilds the pillars.
#include "world/level/end/DragonFight.hpp"

#include "network/session/Session.hpp"
#include "server/Server.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/end/EndGen.hpp"
#include "world/level/levelgen/vanilla/JavaRandom.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>

namespace perlite {
    namespace fs = std::filesystem;
    namespace {
        uint16_t B(const char* name) { return BlockCatalog::Instance().DefaultBlock(name); }
        std::string NameOf(uint16_t id) { return BlockRegistry::Instance().Name(id); }
        constexpr double kEye = 1.62;
    }

    void DragonFight::Load(const std::string& dir) {
        directory = dir;
        crystalGone.assign(10, false);
        loaded = true;
        if (dir.empty())
            return;
        std::ifstream in(fs::path(dir) / "dragon.txt");
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream words(line);
            std::string key;
            words >> key;
            if (key == "killed")
                words >> killed;
            else if (key == "previously_killed")
                words >> previouslyKilled;
            else if (key == "portal_y")
                words >> portalY;
            else if (key == "portal_active")
                words >> podiumActive;
            else if (key == "gateways")
                words >> gatewaysOpened;
            else if (key == "crystal_gone") {
                int i;
                while (words >> i)
                    if (i >= 0 && i < 10)
                        crystalGone[static_cast<size_t>(i)] = true;
            } else if (key == "gateway") {
                std::array<int, 3> from{}, to{};
                if (words >> from[0] >> from[1] >> from[2] >> to[0] >> to[1] >> to[2])
                    gatewayExits[from] = to;
            } else if (key == "credits") {
                std::string name;
                std::getline(words >> std::ws, name);
                if (!name.empty())
                    creditsSeen.insert(name);
            }
        }
        loaded = true;
    }

    void DragonFight::Save() const {
        if (directory.empty())
            return;
        const fs::path path = fs::path(directory) / "dragon.txt";
        std::ofstream out(path.string() + ".tmp", std::ios::trunc);
        out << "killed " << killed << '\n'
            << "previously_killed " << previouslyKilled << '\n'
            << "portal_y " << portalY << '\n'
            << "portal_active " << podiumActive << '\n'
            << "gateways " << gatewaysOpened << '\n'
            << "crystal_gone";
        for (size_t i = 0; i < crystalGone.size(); i++)
            if (crystalGone[i])
                out << ' ' << i;
        out << '\n';
        for (const auto& [from, to] : gatewayExits)
            out << "gateway " << from[0] << ' ' << from[1] << ' ' << from[2] << ' ' << to[0] << ' ' << to[1] << ' ' << to[2]
                << '\n';
        for (const std::string& name : creditsSeen)
            out << "credits " << name << '\n';
        out.close();
        std::error_code ec;
        fs::rename(path.string() + ".tmp", path, ec);
    }

    int DragonFight::Top(int x, int z) const {
        ChunkManager* end = server.LoadedWorld(kEnd);
        if (!end || !end->Peek({x >> 4, z >> 4}))
            return 64;
        for (int y = 255; y >= 0; y--)
            if (BlockSolid(end->GetBlock(x, y, z)))
                return y + 1;
        return 0;
    }

    bool DragonFight::ChunksReady(int x0, int z0, int x1, int z1) {
        ChunkManager& end = server.World(kEnd);
        bool ready = true;
        for (int cx = x0 >> 4; cx <= x1 >> 4; cx++)
            for (int cz = z0 >> 4; cz <= z1 >> 4; cz++)
                if (!end.Peek({cx, cz})) {
                    end.Request({cx, cz});
                    ready = false;
                }
        return ready;
    }

    std::array<double, 3> DragonFight::Node(int index) {
        if (nodes.empty()) {
            // As the game's dragon path finder lays them out: x and z from the ring's angle, y a little
            // above the ground there (the middle ring higher).
            for (int i = 0; i < 24; i++) {
                int x, z, lift = 5;
                if (i < 12) {
                    const double a = 2.0 * (-std::numbers::pi + 0.2617994 * i);
                    x = static_cast<int>(std::floor(60.0 * std::cos(a)));
                    z = static_cast<int>(std::floor(60.0 * std::sin(a)));
                } else if (i < 20) {
                    const double a = 2.0 * (-std::numbers::pi + 0.3926991 * (i - 12));
                    x = static_cast<int>(std::floor(40.0 * std::cos(a)));
                    z = static_cast<int>(std::floor(40.0 * std::sin(a)));
                    lift += 10;
                } else {
                    const double a = 2.0 * (-std::numbers::pi + 0.7853982 * (i - 20));
                    x = static_cast<int>(std::floor(20.0 * std::cos(a)));
                    z = static_cast<int>(std::floor(20.0 * std::sin(a)));
                }
                const int y = std::max(10, Top(x, z) + lift);
                nodes.push_back({x + 0.5, static_cast<double>(y), z + 0.5});
            }
        }
        return nodes[static_cast<size_t>(std::clamp(index, 0, 23))];
    }

    int DragonFight::NearestNode(double x, double y, double z) {
        int best = 0;
        double bestD = 1e300;
        for (int i = 0; i < 24; i++) {
            const auto n = Node(i);
            const double d = (n[0] - x) * (n[0] - x) + (n[1] - y) * (n[1] - y) + (n[2] - z) * (n[2] - z);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    std::vector<std::array<int, 3>> DragonFight::Anchors() const {
        std::vector<std::array<int, 3>> out{{0, 0, 9}};
        for (const Trip& t : trips) {
            const double len = std::max(1e-6, std::hypot(t.gateway[0], t.gateway[2]));
            const double dx = t.gateway[0] / len, dz = t.gateway[2] / len;
            if (t.stage == 0) {
                for (int k = -16; k <= 16; k += 2)
                    out.push_back({static_cast<int>(std::floor(dx * (1024.0 + 16.0 * k))) >> 4,
                                   static_cast<int>(std::floor(dz * (1024.0 + 16.0 * k))) >> 4, 1});
            } else {
                out.push_back({static_cast<int>(std::floor(t.ex)) >> 4, static_cast<int>(std::floor(t.ez)) >> 4, 2});
            }
        }
        return out;
    }

    int DragonFight::CrystalsAlive() const {
        int n = 0;
        ActorManager& mobs = server.MobsIn(kEnd);
        for (const auto& [pillar, id] : pillarCrystal) {
            const Mob* m = mobs.Find(id);
            n += m && m->deathTime == 0;
        }
        return n;
    }

    std::vector<uint64_t> DragonFight::PillarCrystals() const {
        std::vector<uint64_t> out;
        for (const auto& [pillar, id] : pillarCrystal)
            out.push_back(id);
        return out;
    }

    void DragonFight::OnChunkReady(int chunkX, int chunkZ) {
        if (!loaded)
            return;
        const auto& spikes = theend::Spikes(server.Config().seed);
        ActorManager& mobs = server.MobsIn(kEnd);
        for (size_t i = 0; i < spikes.size(); i++) {
            const theend::Spike& s = spikes[i];
            if ((s.x >> 4) != chunkX || (s.z >> 4) != chunkZ || crystalGone[i])
                continue;
            if (const auto it = pillarCrystal.find(static_cast<int>(i)); it != pillarCrystal.end() && mobs.Find(it->second))
                continue;
            // Only on a pillar that still has its bedrock top (a crystal placed on a broken pillar
            // would float).
            if (NameOf(server.BlockAt(kEnd, s.x, s.height, s.z)) != "minecraft:bedrock")
                continue;
            if (Mob* c = mobs.SpawnCrystal(s.x + 0.5, s.height + 1, s.z + 0.5, true)) {
                pillarCrystal[static_cast<int>(i)] = c->id;
                if (server.BlockAt(kEnd, s.x, s.height + 1, s.z) == BlockRegistry::kAir)
                    server.SetBlockIn(kEnd, s.x, s.height + 1, s.z, B("minecraft:fire"));
            }
        }
    }

    void DragonFight::CrystalDestroyed(uint64_t id) {
        for (auto it = pillarCrystal.begin(); it != pillarCrystal.end(); ++it)
            if (it->second == id) {
                crystalGone[static_cast<size_t>(it->first)] = true;
                pillarCrystal.erase(it);
                Save();
                break;
            }
        if (respawnStage >= 0 && std::ranges::find(respawnCrystals, id) != respawnCrystals.end() && respawnStage < 3) {
            // Breaking one of the four stops the respawn; the portal comes back.
            respawnStage = -1;
            respawnCrystals.clear();
            PlacePodium(true);
        }
    }

    void DragonFight::PlacePodium(bool active) {
        if (portalY < 0)
            return;
        const uint16_t bedrock = B("minecraft:bedrock"), endStone = B("minecraft:end_stone"), portal = B("minecraft:end_portal");
        const int oy = portalY;
        for (int dx = -4; dx <= 4; dx++)
            for (int dz = -4; dz <= 4; dz++)
                for (int dy = -1; dy <= 32; dy++) {
                    const int d2 = dx * dx + dy * dy + dz * dz;
                    const bool inside = d2 < 6.25, ring = d2 < 12.25;
                    if (!inside && !ring)
                        continue;
                    uint16_t block;
                    if (dy < 0)
                        block = inside ? bedrock : endStone;
                    else if (dy > 0)
                        block = BlockRegistry::kAir;
                    else if (!inside)
                        block = bedrock;
                    else
                        block = active ? portal : BlockRegistry::kAir;
                    if (server.BlockAt(kEnd, dx, oy + dy, dz) != block)
                        server.SetBlockIn(kEnd, dx, oy + dy, dz, block);
                }
        for (int i = 0; i < 4; i++)
            server.SetBlockIn(kEnd, 0, oy + i, 0, bedrock);
        // Wall torches on the four sides of the pillar, as a torch placed against each side.
        static const std::array<std::pair<std::array<int, 2>, const char*>, 4> kTorches = {{
            {{0, -1}, "south"}, {{0, 1}, "north"}, {{-1, 0}, "east"}, {{1, 0}, "west"}}};
        for (const auto& [at, facing] : kTorches)
            server.SetBlockIn(kEnd, at[0], oy + 2, at[1],
                              BlockCatalog::Instance().Resolve(std::string("minecraft:torch[torch_facing_direction=") + facing + "]"));
        podiumActive = active;
    }

    void DragonFight::PlaceGateway(int x, int y, int z) {
        const uint16_t bedrock = B("minecraft:bedrock"), gateway = B("minecraft:end_gateway");
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -2; dy <= 2; dy++)
                for (int dz = -1; dz <= 1; dz++) {
                    const bool sameX = dx == 0, sameY = dy == 0, sameZ = dz == 0, edgeY = std::abs(dy) == 2;
                    uint16_t block = BlockRegistry::kAir;
                    if (sameX && sameY && sameZ)
                        block = gateway;
                    else if (sameY)
                        block = BlockRegistry::kAir;
                    else if (edgeY && sameX && sameZ)
                        block = bedrock;
                    else if ((sameX || sameZ) && !edgeY)
                        block = bedrock;
                    server.SetBlockIn(kEnd, x + dx, y + dy, z + dz, block);
                }
    }

    void DragonFight::SpawnGateway() {
        const auto& slots = theend::Gateways(server.Config().seed);
        if (gatewaysOpened >= static_cast<int>(slots.size()))
            return;
        const theend::GatewaySlot& g = slots[static_cast<size_t>(gatewaysOpened++)];
        server.World(kEnd).Load({g.x >> 4, g.z >> 4});
        PlaceGateway(g.x, g.y, g.z);
        for (Session* s : server.PlayersIn(kEnd))
            s->PlaySoundTo("random.explode", g.x + 0.5, g.y + 0.5, g.z + 0.5, 4.0f, 1.0f);
    }

    void DragonFight::DragonKilled(uint64_t id) {
        if (id != dragon)
            return;
        dragon = 0;
        for (Session* s : server.PlayersIn(kEnd))
            HideBossBar(*s);
        barShown.clear();
        SpawnGateway();
        PlacePodium(true);
        if (!previouslyKilled)
            server.SetBlockIn(kEnd, 0, portalY + 4, 0, B("minecraft:dragon_egg"));
        previouslyKilled = true;
        killed = true;
        Save();
    }

    void DragonFight::HideBossBar(Session& who) {
        if (!barShown.erase(who.Id()))
            return;
        who.BossBar(dragon, 2, "", 0.0f);
    }

    void DragonFight::UpdateBossBar() {
        ActorManager& mobs = server.MobsIn(kEnd);
        const Mob* d = dragon ? mobs.Find(dragon) : nullptr;
        const std::vector<Session*> players = server.PlayersIn(kEnd);
        std::erase_if(barShown, [&](uint64_t id) {
            Session* s = server.FindSession(id);
            if (!s || s->DimensionId() != kEnd) {
                if (s && d)
                    s->BossBar(d->id, 2, "", 0.0f);
                return true;
            }
            return false;
        });
        if (!d) {
            for (Session* s : players)
                HideBossBar(*s);
            return;
        }
        const float health = std::max(0.0f, d->health) / std::max(1.0f, d->maxHealth > 0 ? d->maxHealth : d->def->health);
        for (Session* s : players) {
            // Players within 192 blocks of the island's centre see the bar.
            const bool near = std::hypot(s->X(), s->Z()) < 192.0;
            if (near && !barShown.contains(s->Id())) {
                s->BossBar(d->id, 0, "Ender Dragon", health);
                barShown.insert(s->Id());
            } else if (!near) {
                HideBossBar(*s);
            } else if (health != barHealth) {
                s->BossBar(d->id, 4, "Ender Dragon", health);
            }
        }
        barHealth = health;
    }

    void DragonFight::Tick() {
        if (!loaded)
            return;
        ChunkManager* end = server.LoadedWorld(kEnd);
        if (!end)
            return;
        tickCount++;
        TickGatewayTrips();
        if (server.PlayersIn(kEnd).empty())
            return;
        if (portalY < 0) {
            // The podium goes where the surface is at the centre of the island.
            if (!ChunksReady(-16, -16, 15, 15))
                return;
            int y = Top(0, 0) - 1;
            while (y > 0 && NameOf(server.BlockAt(kEnd, 0, y, 0)) == "minecraft:bedrock")
                y--;
            portalY = std::max(1, y);
            nodes.clear();
            PlacePodium(killed);
            Save();
        }
        if (tickCount % 100 == 1)
            ChunksReady(-9 * 16, -9 * 16, 9 * 16 + 15, 9 * 16 + 15);
        ActorManager& mobs = server.MobsIn(kEnd);
        const Mob* d = dragon ? mobs.Find(dragon) : nullptr;
        if (!d)
            dragon = 0;
        if (!killed && !dragon && respawnStage < 0) {
            if (!ChunksReady(-16, -16, 15, 15))
                return;
            if (Mob* m = mobs.SpawnDragon(0.5, 128.0, 0.5)) {
                dragon = m->id;
                barShown.clear();
                barHealth = -1;
            }
        }
        if (respawnStage >= 0)
            TickRespawn();
        if (tickCount % 5 == 0)
            UpdateBossBar();
    }

    bool DragonFight::UseEndCrystal(Session& by, int x, int y, int z) {
        const std::string& below = NameOf(server.BlockAt(by.DimensionId(), x, y, z));
        if (below != "minecraft:obsidian" && below != "minecraft:bedrock")
            return false;
        if (server.BlockAt(by.DimensionId(), x, y + 1, z) != BlockRegistry::kAir)
            return false;
        ActorManager& mobs = server.MobsIn(by.DimensionId());
        for (const Mob& m : mobs.All())
            if (m.deathTime == 0 && (m.kind == actorkind::Mob || m.kind == actorkind::Crystal) && std::abs(m.x - (x + 0.5)) < 1.0 && std::abs(m.z - (z + 0.5)) < 1.0 && m.y >= y + 1 - 1.0 &&
                m.y < y + 3)
                return false;
        if (!mobs.SpawnCrystal(x + 0.5, y + 1, z + 0.5, false))
            return false;
        if (!by.Creative()) {
            by.Inventory().TakeHeld(1);
            by.SendSlot(by.Inventory().selected);
        }
        // Four crystals on the exit portal's rim bring the dragon back.
        if (by.DimensionId() != kEnd || !killed || respawnStage >= 0 || portalY < 0)
            return true;
        static const int kRim[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
        std::vector<uint64_t> found;
        for (const auto& r : kRim) {
            uint64_t at = 0;
            for (const Mob& m : mobs.All())
                if (m.kind == actorkind::Crystal && m.deathTime == 0 && static_cast<int>(std::floor(m.x)) == r[0] &&
                    static_cast<int>(std::floor(m.z)) == r[1] && static_cast<int>(std::floor(m.y)) == portalY + 1)
                    at = m.id;
            if (!at)
                return true;
            found.push_back(at);
        }
        respawnCrystals = found;
        respawnStage = 0;
        respawnTicks = 0;
        PlacePodium(false);
        Save();
        return true;
    }

    void DragonFight::TickRespawn() {
        ActorManager& mobs = server.MobsIn(kEnd);
        const auto beamAll = [&](bool on, int x, int y, int z) {
            for (uint64_t id : respawnCrystals)
                if (Mob* c = mobs.Find(id))
                    mobs.SetBeam(*c, on, x, y, z);
        };
        const auto growl = [&] {
            for (Session* s : server.PlayersIn(kEnd))
                s->PlaySoundTo("mob.enderdragon.growl", 0.5, 128.0, 0.5, 4.0f, 1.0f);
        };
        const int t = respawnTicks++;
        const auto& spikes = theend::Spikes(server.Config().seed);
        switch (respawnStage) {
        case 0: // the crystals point at the sky above the portal
            beamAll(true, 0, 128, 0);
            respawnStage = 1;
            respawnTicks = 0;
            break;
        case 1: // a hundred ticks of growling
            if (t < 100) {
                if (t == 0 || t == 50 || t == 51 || t == 52 || t >= 95)
                    growl();
            } else {
                respawnStage = 2;
                respawnTicks = 0;
            }
            break;
        case 2: { // each pillar in turn: the beam moves to it, then it is blown out and rebuilt
            const bool first = t % 40 == 0, last = t % 40 == 39;
            if (!first && !last)
                break;
            const size_t index = static_cast<size_t>(t / 40);
            if (index >= spikes.size()) {
                if (first) {
                    respawnStage = 3;
                    respawnTicks = 0;
                }
                break;
            }
            const theend::Spike& s = spikes[index];
            if (first) {
                beamAll(true, s.x, s.height + 1, s.z);
                break;
            }
            if (!ChunksReady(s.x - 12, s.z - 12, s.x + 12, s.z + 12)) {
                respawnTicks--; // try again next tick
                break;
            }
            // The pillar's old crystal goes quietly; the new one comes with the rebuilt pillar.
            if (const auto it = pillarCrystal.find(static_cast<int>(index)); it != pillarCrystal.end()) {
                mobs.Remove(it->second);
                pillarCrystal.erase(it);
            }
            for (int x = s.x - 10; x <= s.x + 10; x++)
                for (int z = s.z - 10; z <= s.z + 10; z++)
                    for (int y = s.height - 10; y <= s.height + 10; y++)
                        if (y >= 0 && server.BlockAt(kEnd, x, y, z) != BlockRegistry::kAir)
                            server.SetBlockIn(kEnd, x, y, z, BlockRegistry::kAir);
            mobs.Explosion(s.x + 0.5, s.height, s.z + 0.5, 5.0f, 0, true);
            theend::PlaceSpike(s, [&](int x, int y, int z, uint16_t block) {
                if (server.BlockAt(kEnd, x, y, z) != block)
                    server.SetBlockIn(kEnd, x, y, z, block);
            });
            if (Mob* c = mobs.SpawnCrystal(s.x + 0.5, s.height + 1, s.z + 0.5, true)) {
                pillarCrystal[static_cast<int>(index)] = c->id;
                mobs.SetBeam(*c, true, 0, 128, 0);
                server.SetBlockIn(kEnd, s.x, s.height + 1, s.z, B("minecraft:fire"));
            }
            crystalGone[index] = false;
            break;
        }
        case 3: // the dragon
            if (t >= 100) {
                for (const auto& [pillar, id] : pillarCrystal)
                    if (Mob* c = mobs.Find(id))
                        mobs.SetBeam(*c, false);
                for (uint64_t id : respawnCrystals)
                    if (Mob* c = mobs.Find(id)) {
                        const double x = c->x, y = c->y, z = c->z;
                        c->deathTime = 1;
                        for (Session* p : server.PlayersIn(kEnd))
                            p->HideItem(id);
                        mobs.QueueExplosion(x, y, z, 6.0f, id, false);
                    }
                respawnCrystals.clear();
                respawnStage = -1;
                killed = false;
                if (Mob* m = mobs.SpawnDragon(0.5, 128.0, 0.5)) {
                    dragon = m->id;
                    barShown.clear();
                    barHealth = -1;
                }
                Save();
            } else if (t >= 80) {
                growl();
            } else if (t == 0) {
                beamAll(true, 0, 128, 0);
            } else if (t < 5) {
                growl();
            }
            break;
        default:
            respawnStage = -1;
        }
    }

    bool DragonFight::FirstExit(Session& who) {
        if (!creditsSeen.insert(who.Name()).second)
            return false;
        Save();
        return true;
    }

    // An inner gateway sends players about a thousand blocks out, to the outer islands, building a
    // gateway there that leads back; the trip waits while the chunks out there generate.
    void DragonFight::EnterGateway(Session& who, int x, int y, int z) {
        // The gateway block itself (the player may stand in the block above or below it).
        std::array<int, 3> gate{x, y, z};
        bool found = false;
        for (int dy = -1; dy <= 1 && !found; dy++)
            if (NameOf(server.BlockAt(kEnd, x, y + dy, z)) == "minecraft:end_gateway") {
                gate = {x, y + dy, z};
                found = true;
            }
        if (!found)
            return;
        for (const Trip& t : trips)
            if (t.player == who.Id())
                return;
        Trip trip;
        trip.player = who.Id();
        trip.gateway = gate;
        // A gateway that already knows its exit only waits for the chunks there.
        if (const auto exit = gatewayExits.find(gate); exit != gatewayExits.end()) {
            trip.stage = 2;
            trip.ex = exit->second[0];
            trip.ez = exit->second[2];
        }
        trips.push_back(trip);
    }

    void DragonFight::SendThrough(Session& who, const std::array<int, 3>& gate, const std::array<int, 3>& to) {
        // Onto the tallest block near the exit (not bedrock), or on top of the exit's frame.
        int bx = to[0], by = to[1] + 2, bz = to[2], best = -1;
        for (int dx = -5; dx <= 5; dx++)
            for (int dz = -5; dz <= 5; dz++) {
                if (dx == 0 && dz == 0)
                    continue;
                for (int yy = 255; yy > std::max(best, 0); yy--) {
                    const uint16_t b = server.BlockAt(kEnd, to[0] + dx, yy, to[2] + dz);
                    if (BlockSolid(b) && NameOf(b) != "minecraft:bedrock") {
                        best = yy;
                        bx = to[0] + dx;
                        by = yy;
                        bz = to[2] + dz;
                        break;
                    }
                }
            }
        who.Teleport(bx + 0.5, by + 1 + kEye, bz + 0.5);
        for (Session* s : server.PlayersIn(kEnd))
            s->PlaySoundTo("mob.endermen.portal", gate[0] + 0.5, gate[1] + 0.5, gate[2] + 0.5, 1.0f, 1.0f);
    }


    void DragonFight::TickGatewayTrips() {
        for (auto it = trips.begin(); it != trips.end();) {
            Trip& t = *it;
            Session* who = server.FindSession(t.player);
            if (!who || who->DimensionId() != kEnd || ++t.ticks > 600) {
                it = trips.erase(it);
                continue;
            }
            const double len = std::max(1e-6, std::hypot(t.gateway[0], t.gateway[2]));
            const double dx = t.gateway[0] / len, dz = t.gateway[2] / len;
            ChunkManager& end = server.World(kEnd);
            const auto empty = [&](double px, double pz) {
                const ChunkColumn* c = end.Column(static_cast<int>(std::floor(px)) >> 4, static_cast<int>(std::floor(pz)) >> 4);
                if (!c)
                    return true;
                for (int s = 0; s < kSections; s++) {
                    const ChunkColumn::Section& sec = c->SectionAt(s);
                    if (sec.cells || sec.uniform != BlockRegistry::kAir)
                        return false;
                }
                return true;
            };
            if (t.stage == 2) {
                const auto exit = gatewayExits.find(t.gateway);
                if (exit == gatewayExits.end()) {
                    it = trips.erase(it);
                    continue;
                }
                const auto to = exit->second;
                if (!ChunksReady(to[0] - 16, to[2] - 16, to[0] + 16, to[2] + 16)) {
                    ++it;
                    continue;
                }
                const auto gate = t.gateway;
                it = trips.erase(it);
                SendThrough(*who, gate, to);
                continue;
            }
            if (t.stage == 0) {
                // The columns 1024 blocks out, and 16 steps of 16 either way of it.
                bool ready = true;
                for (int k = -16; k <= 16; k++) {
                    const double px = dx * (1024.0 + 16.0 * k), pz = dz * (1024.0 + 16.0 * k);
                    const ChunkPos p{static_cast<int>(std::floor(px)) >> 4, static_cast<int>(std::floor(pz)) >> 4};
                    if (!end.Peek(p)) {
                        end.Request(p);
                        ready = false;
                    }
                }
                if (!ready) {
                    ++it;
                    continue;
                }
                double px = dx * 1024.0, pz = dz * 1024.0;
                for (int i = 16; !empty(px, pz) && i-- > 0;)
                    px -= dx * 16.0, pz -= dz * 16.0;
                for (int i = 16; empty(px, pz) && i-- > 0;)
                    px += dx * 16.0, pz += dz * 16.0;
                t.ex = px;
                t.ez = pz;
                t.stage = 1;
            }
            const int cx = static_cast<int>(std::floor(t.ex)) >> 4, cz = static_cast<int>(std::floor(t.ez)) >> 4;
            if (!ChunksReady(cx * 16 - 16, cz * 16 - 16, cx * 16 + 31, cz * 16 + 31)) {
                ++it;
                continue;
            }
            // A spot on end stone with two blocks of air above, nearest the chunk's centre.
            const uint16_t endStone = B("minecraft:end_stone");
            bool island = false;
            int ix = 0, iy = 0, iz = 0;
            double bestD = 1e300;
            for (int lx = 0; lx < 16; lx++)
                for (int lz = 0; lz < 16; lz++)
                    for (int y = 254; y >= 1; y--) {
                        const int wx = cx * 16 + lx, wz = cz * 16 + lz;
                        if (end.GetBlock(wx, y, wz) != endStone || end.GetBlock(wx, y + 1, wz) != BlockRegistry::kAir ||
                            end.GetBlock(wx, y + 2, wz) != BlockRegistry::kAir)
                            continue;
                        const double d = (lx - 8.0) * (lx - 8.0) + (lz - 8.0) * (lz - 8.0) + (y - 8.0) * (y - 8.0) * 0.0;
                        if (d < bestD) {
                            bestD = d;
                            ix = wx;
                            iy = y;
                            iz = wz;
                            island = true;
                        }
                        break;
                    }
            if (!island) {
                // Nothing out there: a small island of end stone at y 75, as the game's end island
                // feature makes it.
                ix = static_cast<int>(std::floor(t.ex + 0.5));
                iy = 75;
                iz = static_cast<int>(std::floor(t.ez + 0.5));
                vanilla::WorldRandom r = vanilla::WorldRandom::Legacy(server.Config().seed ^ (static_cast<int64_t>(ix) * 31 + iz));
                float size = static_cast<float>(r.NextInt(3)) + 4.0f;
                for (int i = 0; size > 0.5f; i--) {
                    for (int j = static_cast<int>(std::floor(-size)); j <= static_cast<int>(std::ceil(size)); j++)
                        for (int k = static_cast<int>(std::floor(-size)); k <= static_cast<int>(std::ceil(size)); k++)
                            if (static_cast<float>(j * j + k * k) <= (size + 1.0f) * (size + 1.0f))
                                server.SetBlockIn(kEnd, ix + j, iy + i, iz + k, endStone);
                    size -= static_cast<float>(r.NextInt(2)) + 0.5f;
                }
            }
            // The exit gateway: ten blocks above the tallest block within 16 of that spot.
            int tx = ix, ty = iy, tz = iz;
            for (int ddx = -16; ddx <= 16; ddx++)
                for (int ddz = -16; ddz <= 16; ddz++)
                    for (int y = 254; y > ty; y--)
                        if (BlockSolid(end.GetBlock(ix + ddx, y, iz + ddz))) {
                            tx = ix + ddx;
                            ty = y;
                            tz = iz + ddz;
                            break;
                        }
            const std::array<int, 3> exit{tx, std::min(ty + 10, 250), tz};
            PlaceGateway(exit[0], exit[1], exit[2]);
            gatewayExits[t.gateway] = exit;
            gatewayExits[exit] = t.gateway;
            Save();
            t.stage = 2;
            ++it;
        }
    }
}
