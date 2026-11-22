#include "network/session/SessionInternal.hpp"

#include <chrono>
#include <cmath>

namespace perlite {
    void Session::OnChunkRadius(std::span<const uint8_t> body) {
        proto::RequestChunkRadiusPacket req;
        if (!Decode(body, req) || state == State::AwaitSettings || state == State::AwaitLogin)
            return;
        radius = std::clamp<int32_t>(req.chunkRadius, 2, static_cast<int32_t>(server.Config().viewDistance));
        proto::ChunkRadiusUpdatePacket reply;
        reply.chunkRadius = radius;
        Send(reply);
        PublishCenter(true);
    }

    void Session::SwitchDimension(int to, double x, double y, double z) {
        proto::ChangeDimensionPacket change;
        change.dimension = to;
        change.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        change.respawn = false;
        Send(change);
        dimension = to;
        awaitingDimensionAck = true;
        dimensionChangedAt = std::chrono::steady_clock::now();
        sent.clear();
        posX = x;
        posY = y;
        posZ = z;
        moved = true;
        PublishCenter(true);
    }

    void Session::PublishCenter(bool force) {
        const ChunkPos now{static_cast<int32_t>(std::floor(posX)) >> 4,
                           static_cast<int32_t>(std::floor(posZ)) >> 4};
        if (!force && centerSent && now == center)
            return;
        center = now;
        centerSent = true;
        if (radius == 0)
            return;
        proto::NetworkChunkPublisherUpdatePacket u;
        u.coordinates = {static_cast<int32_t>(std::floor(posX)), static_cast<int32_t>(std::floor(posY)),
                         static_cast<int32_t>(std::floor(posZ))};
        u.radius = static_cast<uint32_t>(radius * 16);
        Send(u);
        for (auto it = sent.begin(); it != sent.end();) {
            if (std::abs(it->x - center.x) > radius + 1 || std::abs(it->z - center.z) > radius + 1)
                it = sent.erase(it);
            else
                ++it;
        }
    }

    bool Session::WantsChunk(ChunkPos pos) const {
        if (radius == 0 || sent.contains(pos))
            return false;
        const int dx = pos.x - center.x, dz = pos.z - center.z;
        return dx * dx + dz * dz <= radius * radius;
    }

    // A chunk is the same packet for every player, so it is encoded and compressed once per change
    // of the chunk and sent as its own batch (after anything already queued, to keep the order).
    void Session::SendChunk(ChunkEntry& entry, ChunkPos pos) {
        const std::vector<uint8_t>& payload = server.World(dimension).Payload(entry);
        const auto build = [&] {
            proto::LevelChunkPacket lc;
            lc.x = pos.x;
            lc.z = pos.z;
            lc.dimension = dimension;
            lc.subChunkCount = static_cast<uint32_t>(RangeOf(dimension).SectionCount());
            lc.cacheEnabled = false;
            lc.payload = payload;
            return lc;
        };
        const ServerConfig& cfg = server.Config();
        if (cfg.debugLogPackets || !cfg.debugSkipPackets.empty()) {
            Send(build());
            return;
        }
        const uint32_t settings = codec.SettingsKey();
        if (entry.sealedVersion != entry.payloadVersion || entry.sealedSettings != settings) {
            std::vector<uint8_t> body;
            if (!net::EncodeChecked(build(), body)) {
                LogWarn("refusing to send malformed level_chunk");
                return;
            }
            std::vector<uint8_t> content;
            BatchCodec::AppendPacket(content, proto::LevelChunkPacket::kId, body);
            entry.sealed = codec.Seal(content);
            entry.sealedVersion = entry.payloadVersion;
            entry.sealedSettings = settings;
        }
        Flush();
        if (state == State::Closed)
            return;
        bool ok;
        if (cipher.Active()) {
            const std::vector<uint8_t> wire = cipher.Encrypt(entry.sealed);
            ok = connection->Send(wire.data(), wire.size(), true);
        } else {
            ok = connection->Send(entry.sealed.data(), entry.sealed.size(), true);
        }
        if (!ok)
            Close();
    }

    void Session::StreamChunks() {
        if (radius == 0 || !centerSent)
            return;
        std::vector<ChunkPos> want;
        for (int dx = -radius; dx <= radius; dx++)
            for (int dz = -radius; dz <= radius; dz++) {
                const ChunkPos p{center.x + dx, center.z + dz};
                if (WantsChunk(p))
                    want.push_back(p);
            }
        std::ranges::sort(want, [this](const ChunkPos& a, const ChunkPos& b) {
            const int da = (a.x - center.x) * (a.x - center.x) + (a.z - center.z) * (a.z - center.z);
            const int db = (b.x - center.x) * (b.x - center.x) + (b.z - center.z) * (b.z - center.z);
            return da < db;
        });
        int budget = connection->Local() ? kChunksPerTick * 8
                     : state == State::Loading ? kChunksPerTick * 3
                                               : kChunksPerTick * 2;
        // Chunks go out nearest first: once a nearer chunk is still generating, farther ready chunks
        // wait (beyond one ring of slack) so the world fills in from the player outwards instead of
        // showing detached islands. Farther chunks are still requested so the workers stay busy.
        int asked = 0, sentNow = 0;
        double sendLimit = 1e9;
        for (const ChunkPos& pos : want) {
            const int dx = pos.x - center.x, dz = pos.z - center.z;
            const double ring = std::sqrt(static_cast<double>(dx * dx + dz * dz));
            auto entry = server.World(dimension).Get(pos);
            if (!entry) {
                sendLimit = std::min(sendLimit, ring + 1.5);
                if (++asked >= 192)
                    break;
                continue;
            }
            if (ring > sendLimit || budget <= 0)
                continue;
            if (sentNow > 0 && std::chrono::steady_clock::now() > server.ChunkSendDeadline()) {
                budget = 0;
                continue;
            }
            SendChunk(*entry, pos);
            sent.insert(pos);
            budget--;
            sentNow++;
        }
        if (!spawnSent && state == State::Loading) {
            size_t near = 0;
            for (const ChunkPos& p : sent)
                if (std::abs(p.x - center.x) <= 1 && std::abs(p.z - center.z) <= 1)
                    near++;
            if (near >= 9 || (radius < 2 && !sent.empty())) {
                proto::PlayStatusPacket spawn;
                spawn.status = proto::PlayStatusPacketStatus::PlayerSpawn;
                Send(spawn);
                spawnSent = true;
            }
        }
    }

    void Session::OnInitialized() {
        if (state != State::Loading)
            return;
        state = State::InGame;
        server.OnPlayerJoined(*this);
    }
}
