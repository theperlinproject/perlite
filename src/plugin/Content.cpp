// Content mods (Perlin API revision 3): entities, items, blocks and biomes registered by server mods,
// simulated on their vanilla bases, announced to the client mods that draw them over the mod channel.
#include "server/Server.hpp"

#include "world/actor/ActorManager.hpp"
#include "world/actor/mob/MobRegistry.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"
#include "world/loot/Loot.hpp"

#include <algorithm>
#include <cmath>

namespace perlite {
#if PERLITE_PERLIN_REV3
    namespace {
        using abi::Status;
        template <class T> using Result = abi::host::Result<T>;
        std::unexpected<Status> Fail(Status s) { return std::unexpected(s); }
        std::string FullName(const std::string& n) { return !n.contains(':') ? "minecraft:" + n : n; }
        constexpr int kTopY = kMinY + kHeight - 1;
    }

    struct Server::ContentState {
        struct Biome {
            abi::ContentBiome def;
            int base = -1;
            std::vector<int> replaces;
            uint16_t surface = 0, filler = 0;
        };
        std::vector<abi::ContentEntry> entries;
        std::vector<abi::ContentEntity> entities;
        std::vector<Biome> biomes;
        int ticks = 0;
    };

    namespace {
        std::string Bare(std::string_view s) {
            if (s.starts_with("minecraft:"))
                s.remove_prefix(10);
            return std::string(s);
        }
        bool ContentId(const std::string& id) { return id.contains(':') && !id.starts_with("minecraft:") && id.size() <= 128; }
        std::string Quote(const std::string& s) {
            std::string out = "\"";
            for (const char c : s) {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (static_cast<unsigned char>(c) >= 0x20)
                    out += c;
            }
            return out + "\"";
        }
        std::string DropsJson(const std::vector<abi::ContentDrop>& drops) {
            std::string pools = "{\"pools\":[";
            for (size_t i = 0; i < drops.size(); i++) {
                const abi::ContentDrop& d = drops[i];
                if (i)
                    pools += ',';
                pools += "{\"rolls\":1,\"conditions\":[{\"condition\":\"random_chance\",\"chance\":" + std::to_string(d.chance) +
                         "}],\"entries\":[{\"type\":\"item\",\"name\":" + Quote(d.item) +
                         ",\"functions\":[{\"function\":\"set_count\",\"count\":{\"min\":" + std::to_string(d.min) +
                         ",\"max\":" + std::to_string(std::max(d.min, d.max)) + "}}]}]}";
            }
            return pools + "]}";
        }
        int BiomeIndex(const std::string& name) {
            const std::string bare = Bare(name);
            for (int i = 0; i < vanilla::kBiomeCount; i++)
                if (bare == vanilla::kBiomes[i].java)
                    return i;
            return -1;
        }
    }

    void Server::EnsureContent() {
        if (!content)
            content = std::make_shared<ContentState>();
    }

    Result<void> Server::OpContentEntityRegister(PluginCall& c, const abi::ContentEntity& r) {
        EnsureContent();
        if (!ContentId(r.id))
            return Fail(Status::BadArguments);
        const MobType* t = MobRegistry::Instance().AddContent(r.id, FullName(r.base), r.clientBase.empty() ? "" : FullName(r.clientBase),
                                                               r.health, r.width, r.height, r.speed, r.attack);
        if (!t)
            return Fail(Status::NotFound);
        if (!r.drops.empty()) {
            std::string error;
            LootTables::Instance().Override("entities/" + r.id.substr(r.id.find(':') + 1), DropsJson(r.drops), error);
        }
        std::erase_if(content->entities, [&](const abi::ContentEntity& e) { return e.id == r.id; });
        content->entities.push_back(r);
        std::erase_if(content->entries, [&](const abi::ContentEntry& e) { return e.kind == "entity" && e.id == r.id; });
        content->entries.push_back({"entity", r.id, c.plugin,
                                    "{\"id\":" + Quote(r.id) + ",\"base\":" + Quote(FullName(r.base)) + ",\"health\":" + std::to_string(t->def.health) +
                                        ",\"width\":" + std::to_string(t->def.width) + ",\"height\":" + std::to_string(t->def.height) + "}"});
        AnnounceContent();
        return {};
    }

    Result<void> Server::OpContentItemRegister(PluginCall& c, const abi::ContentItem& r) {
        EnsureContent();
        if (!ContentId(r.id) || !BlockCatalog::Instance().ItemByName(FullName(r.base)))
            return Fail(Status::BadArguments);
        BlockCatalog::Instance().Alias(r.id, FullName(r.base));
        std::erase_if(content->entries, [&](const abi::ContentEntry& e) { return e.kind == "item" && e.id == r.id; });
        content->entries.push_back({"item", r.id, c.plugin,
                                    "{\"id\":" + Quote(r.id) + ",\"base\":" + Quote(FullName(r.base)) + ",\"name\":" + Quote(r.displayName) +
                                        ",\"icon\":" + Quote(r.icon) + "}"});
        AnnounceContent();
        return {};
    }

    Result<void> Server::OpContentBlockRegister(PluginCall& c, const abi::ContentBlock& r) {
        EnsureContent();
        if (!ContentId(r.id) || !BlockCatalog::Instance().Knows(FullName(r.base)))
            return Fail(Status::BadArguments);
        BlockCatalog::Instance().Alias(r.id, FullName(r.base));
        if (!r.drops.empty()) {
            std::string error;
            LootTables::Instance().Override("blocks/" + r.id.substr(r.id.find(':') + 1), DropsJson(r.drops), error);
        }
        std::erase_if(content->entries, [&](const abi::ContentEntry& e) { return e.kind == "block" && e.id == r.id; });
        content->entries.push_back({"block", r.id, c.plugin, "{\"id\":" + Quote(r.id) + ",\"base\":" + Quote(FullName(r.base)) + "}"});
        AnnounceContent();
        return {};
    }

    Result<void> Server::OpContentBiomeRegister(PluginCall& c, const abi::ContentBiome& r) {
        EnsureContent();
        ContentState::Biome b;
        b.def = r;
        b.base = BiomeIndex(r.base);
        for (const std::string& n : r.replaces)
            if (const int i = BiomeIndex(n); i >= 0)
                b.replaces.push_back(i);
        if (!ContentId(r.id) || b.base < 0 || b.replaces.empty() || r.share < 0 || r.share > 1)
            return Fail(Status::BadArguments);
        if (!r.surface.empty())
            b.surface = BlockCatalog::Instance().DefaultBlock(FullName(r.surface));
        if (!r.filler.empty())
            b.filler = BlockCatalog::Instance().DefaultBlock(FullName(r.filler));
        std::erase_if(content->biomes, [&](const ContentState::Biome& e) { return e.def.id == r.id; });
        content->biomes.push_back(b);
        std::erase_if(content->entries, [&](const abi::ContentEntry& e) { return e.kind == "biome" && e.id == r.id; });
        content->entries.push_back({"biome", r.id, c.plugin,
                                    "{\"id\":" + Quote(r.id) + ",\"base\":" + Quote(FullName(r.base)) + ",\"grass\":" + std::to_string(r.grassColor) +
                                        ",\"foliage\":" + std::to_string(r.foliageColor) + ",\"water\":" + std::to_string(r.waterColor) +
                                        ",\"fog\":" + std::to_string(r.fogColor) + ",\"sky\":" + std::to_string(r.skyColor) + "}"});
        AnnounceContent();
        return {};
    }

    Result<void> Server::OpContentUnregister(PluginCall&, const abi::ContentUnregisterRequest& r) {
        EnsureContent();
        const size_t before = content->entries.size();
        std::erase_if(content->entries, [&](const abi::ContentEntry& e) { return e.kind == r.kind && e.id == r.id; });
        if (content->entries.size() == before)
            return Fail(Status::NotFound);
        if (r.kind == "item" || r.kind == "block")
            BlockCatalog::Instance().Unalias(r.id);
        if (r.kind == "biome")
            std::erase_if(content->biomes, [&](const ContentState::Biome& e) { return e.def.id == r.id; });
        if (r.kind == "entity")
            std::erase_if(content->entities, [&](const abi::ContentEntity& e) { return e.id == r.id; });
        AnnounceContent();
        return {};
    }

    Result<abi::ContentListResponse> Server::OpContentList(PluginCall&) {
        EnsureContent();
        abi::ContentListResponse out;
        out.entries = content->entries;
        return out;
    }

    Result<void> Server::OpModChannelSend(PluginCall&, const abi::ModChannelMessage& m) {
        if (m.channel.empty() || m.channel.size() > 64 || m.body.size() > 65536 || m.channel.starts_with("perlin:"))
            return Fail(Status::BadArguments);
        const std::string body(m.body.begin(), m.body.end());
        if (m.player == 0) {
            for (Session* s : PlayersIn(kOverworld))
                s->SendModChannel(m.channel, body);
            for (Session* s : PlayersIn(kNether))
                s->SendModChannel(m.channel, body);
            for (Session* s : PlayersIn(kEnd))
                s->SendModChannel(m.channel, body);
            return {};
        }
        Session* s = FindSession(m.player);
        if (!s)
            return Fail(Status::NotFound);
        s->SendModChannel(m.channel, body);
        return {};
    }

    void Server::AnnounceContent() {
        EnsureContent();
        std::string list = "[";
        for (size_t i = 0; i < content->entries.size(); i++) {
            const abi::ContentEntry& e = content->entries[i];
            list += (i ? "," : "") + std::string("{\"kind\":") + Quote(e.kind) + ",\"id\":" + Quote(e.id) + ",\"owner\":" + Quote(e.owner) +
                    ",\"def\":" + e.json + "}";
        }
        list += "]";
        for (int d = 0; d < kDimensionCount; d++)
            for (Session* s : PlayersIn(d))
                if (s->contentClient)
                    s->SendModChannel("perlin:content", list);
    }

    void Server::HookModChannel(Session& from, const std::string& channel, const std::string& body) {
        if (channel == "perlin:hello") {
            // A client mod for content: it gets the content list now and the type of every content
            // entity it is shown from now on.
            from.contentClient = true;
            AnnounceContent();
            return;
        }
        if (channel.starts_with("perlin:"))
            return;
        if (plugins->AnyoneListens(abi::Event::ModChannel))
            plugins->Dispatch<abi::Event::ModChannel>({from.Id(), channel, std::vector<uint8_t>(body.begin(), body.end())});
    }

    void Server::ContentOnNewChunk(int dimension, ChunkPos pos, ChunkColumn& column) {
        if (content && !content->biomes.empty() && dimension == kOverworld) {
            // Each content biome takes patches (64 blocks across) of the biomes it replaces.
            for (int lx = 0; lx < 16; lx++)
                for (int lz = 0; lz < 16; lz++) {
                    const int wx = pos.x * 16 + lx, wz = pos.z * 16 + lz;
                    const int here = column.GenBiome(lx, lz);
                    for (const ContentState::Biome& b : content->biomes) {
                        if (std::ranges::find(b.replaces, here) == b.replaces.end())
                            continue;
                        uint64_t h = static_cast<uint64_t>(config.seed) ^ (static_cast<uint64_t>(static_cast<int64_t>(wx >> 6)) * 0x9E3779B97F4A7C15ull) ^
                                     (static_cast<uint64_t>(static_cast<int64_t>(wz >> 6)) * 0xC2B2AE3D27D4EB4Full) ^ std::hash<std::string>{}(b.def.id);
                        h ^= h >> 29;
                        h *= 0xBF58476D1CE4E5B9ull;
                        h ^= h >> 32;
                        if (static_cast<float>(h % 10000) / 10000.0f >= b.def.share)
                            continue;
                        int y = kTopY;
                        while (y > kMinY && column.Get(lx, y, lz) == BlockRegistry::kAir)
                            y--;
                        if (b.surface && y > kMinY)
                            column.Set(lx, y, lz, b.surface);
                        if (b.filler)
                            for (int k = 1; k <= 3 && y - k > kMinY; k++)
                                column.Set(lx, y - k, lz, b.filler);
                        break;
                    }
                }
        }
        if (plugins->AnyoneListens(abi::Event::ChunkGenerated))
            plugins->Dispatch<abi::Event::ChunkGenerated>({static_cast<abi::Dimension>(dimension), pos.x, pos.z});
    }

    void Server::TickContentSpawns() {
        if (!content || content->entities.empty() || ++content->ticks % 200 != 0)
            return;
        // Every ten seconds each content entity's spawn rules try a group near each player.
        for (int d = 0; d < kDimensionCount; d++)
            for (Session* p : PlayersIn(d))
                for (const abi::ContentEntity& e : content->entities)
                    for (const abi::ContentSpawn& rule : e.spawns) {
                        if (static_cast<int>(rule.dimension) != d || worldRng() % 100 >= rule.weight)
                            continue;
                        int near = 0;
                        for (const Mob& m : MobsIn(d).All())
                            near += m.def && e.id == m.def->id && std::hypot(m.x - p->X(), m.z - p->Z()) < 64;
                        if (near >= 8)
                            continue;
                        const double a = static_cast<double>(worldRng() % 6283) / 1000.0, r = 24 + worldRng() % 24;
                        const int x = static_cast<int>(p->X() + std::cos(a) * r), z = static_cast<int>(p->Z() + std::sin(a) * r);
                        ChunkManager* world = LoadedWorld(d);
                        const auto entry = world ? world->Peek({x >> 4, z >> 4}) : nullptr;
                        if (!entry)
                            continue;
                        if (!rule.biomes.empty()) {
                            const int b = entry->column.GenBiome(x & 15, z & 15);
                            bool ok = false;
                            for (const std::string& n : rule.biomes)
                                ok = ok || BiomeIndex(n) == b;
                            if (!ok)
                                continue;
                        }
                        int y = d == kNether ? 120 : kTopY;
                        while (y > kMinY && !BlockSolid(world->GetBlock(x, y, z)))
                            y--;
                        const int count = static_cast<int>(rule.minGroup + worldRng() % (std::max(rule.maxGroup, rule.minGroup) - rule.minGroup + 1));
                        for (int i = 0; i < count; i++)
                            MobsIn(d).Spawn(e.id, x + 0.5 + i % 2, y + 1, z + 0.5 + i / 2, false);
                    }
    }
#else
    // Before API revision 3 there are no content mods; the hooks do nothing.
    struct Server::ContentState {};
    void Server::EnsureContent() {}
    void Server::AnnounceContent() {}
    void Server::HookModChannel(Session&, const std::string&, const std::string&) {}
    void Server::ContentOnNewChunk(int, ChunkPos, ChunkColumn&) {}
    void Server::TickContentSpawns() {}
#endif
}
