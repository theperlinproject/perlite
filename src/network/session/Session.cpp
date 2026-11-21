#include "network/session/SessionInternal.hpp"

namespace perlite {
    Session::Session(Server& server, std::shared_ptr<Connection> connection, uint64_t runtimeId)
        : server(server), connection(std::move(connection)), runtimeId(runtimeId),
          skin(std::make_unique<proto::Skin>()) {
        address = this->connection->Describe();
        posX = server.SpawnX() + 0.5;
        posZ = server.SpawnZ() + 0.5;
        posY = server.SpawnY() + kEyeHeight;
    }

    Session::~Session() = default;

    const proto::Skin& Session::GetSkin() const {
        return *skin;
    }

    void Session::Queue(uint32_t id, const std::vector<uint8_t>& body) {
        if (state == State::Closed)
            return;
        BatchCodec::AppendPacket(outgoing, id, body);
    }

    void Session::Flush() {
        if (outgoing.empty() || state == State::Closed)
            return;
        std::vector<uint8_t> wire = codec.Seal(outgoing);
        outgoing.clear();
        if (cipher.Active())
            wire = cipher.Encrypt(wire);
        if (!connection->Send(wire.data(), wire.size(), true))
            Close();
    }

    void Session::SendImmediate(uint32_t id, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> content;
        BatchCodec::AppendPacket(content, id, body);
        std::vector<uint8_t> wire = codec.Seal(content);
        if (cipher.Active())
            wire = cipher.Encrypt(wire);
        connection->Send(wire.data(), wire.size(), true);
    }

    void Session::Close() {
        if (state == State::Closed)
            return;
        const bool wasInGame = state == State::InGame;
        state = State::Closed;
        if (wasInGame)
            server.OnPlayerLeft(*this);
        connection->Close();
    }

    void Session::Kick(const std::string& reason) {
        if (state == State::Closed)
            return;
        proto::DisconnectPacket p;
        p.reason = proto::DisconnectFailReason::Unknown;
        p.message = reason;
        p.filteredMessage = reason;
        Send(p);
        Flush();
        Close();
    }

    void Session::OnMessage(std::span<const uint8_t> received) {
        std::span<const uint8_t> wire = received;
        if (state == State::Closed)
            return;
        lastPacket = std::chrono::steady_clock::now();
        std::vector<uint8_t> content;
        std::vector<uint8_t> plain;
        if (cipher.Active()) {
            if (!cipher.Decrypt(wire, plain)) {
                LogWarn(connection->Describe() + ": batch failed its checksum, closing");
                Close();
                return;
            }
            wire = plain;
        }
        if (!codec.Open(wire, content)) {
            LogWarn(connection->Describe() + ": corrupt batch, closing");
            Close();
            return;
        }
        if (server.Config().packetRateLimiter && !connection->Local()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - rateWindow >= std::chrono::seconds(1)) {
                rateStrikes = rateCount > kPacketsPerSecond ? rateStrikes + 1 : 0;
                rateWindow = now;
                rateCount = 0;
            }
            if (rateStrikes >= 3) {
                LogWarn((name.empty() ? connection->Describe() : name) + " sent too many packets, disconnecting");
                Kick("disconnectionScreen.badPacket");
                return;
            }
        }
        const bool ok = BatchCodec::Split(content, [this](uint32_t id, std::span<const uint8_t> body) {
            rateCount++;
            if (state != State::Closed)
                Dispatch(id, body);
        });
        if (!ok) {
            LogWarn(connection->Describe() + ": truncated batch, closing");
            Close();
        }
    }

    void Session::Dispatch(uint32_t id, std::span<const uint8_t> body) {
        switch (id) {
        case proto::RequestNetworkSettingsPacket::kId:
            OnRequestSettings(body);
            break;
        case proto::LoginPacket::kId:
            OnLogin(body);
            break;
        case proto::ResourcePackClientResponsePacket::kId:
            OnPackResponse(body);
            break;
        case proto::ResourcePackChunkRequestPacket::kId:
            OnPackChunkRequest(body);
            break;
        case proto::RequestChunkRadiusPacket::kId:
            OnChunkRadius(body);
            break;
        case proto::SetLocalPlayerAsInitializedPacket::kId:
            OnInitialized();
            break;
        case proto::TextPacket::kId:
            OnText(body);
            break;
        case proto::CommandRequestPacket::kId:
            OnCommand(body);
            break;
        case proto::PlayerAuthInputPacket::kId:
            OnAuthInput(body);
            break;
        case proto::MovePlayerPacket::kId:
            OnMovePlayer(body);
            break;
        case proto::ItemStackRequestPacket::kId:
            OnStackRequest(body);
            break;
        case proto::MobEquipmentPacket::kId:
            OnMobEquipment(body);
            break;
        case proto::InventoryTransactionPacket::kId:
            OnTransaction(body);
            break;
        case proto::PlayerActionPacket::kId:
            OnPlayerAction(body);
            break;
        case proto::BlockPickRequestPacket::kId:
            OnPickBlock(body);
            break;
        case proto::InteractPacket::kId:
            OnInteract(body);
            break;
        case proto::ContainerClosePacket::kId:
            OnContainerClose(body);
            break;
        case proto::AnimatePacket::kId:
            OnAnimate(body);
            break;
        case proto::PlayerSkinPacket::kId:
            OnPlayerSkin(body);
            break;
        case proto::TickSyncPacket::kId:
            OnTickSync(body);
            break;
        case proto::NetworkStackLatencyPacket::kId:
            OnLatency(body);
            break;
        case proto::EmotePacket::kId:
            OnEmote(body);
            break;
        case proto::LevelSoundEventPacket::kId:
            OnSound(body);
            break;
        case proto::RespawnPacket::kId:
            OnRespawn(body);
            break;
        case proto::ModalFormResponsePacket::kId:
            OnFormResponse(body);
            break;
        case proto::ClientToServerHandshakePacket::kId:
            OnClientHandshake();
            break;
        case proto::ClientCacheStatusPacket::kId:
        default:
            break;
        }
    }

    void Session::Tick(uint64_t) {
        if (state == State::Closed)
            return;
        if (state == State::InGame)
            SurvivalTick();
        const auto now = std::chrono::steady_clock::now();
        if (!connection->Alive()) {
            Close();
            return;
        }
        if ((state == State::InGame && now - lastPacket > kIdleTimeout) ||
            (state != State::InGame && now - stateSince > kLoginTimeout)) {
            LogInfo((name.empty() ? connection->Describe() : name) + " timed out");
            Close();
            return;
        }
        const uint32_t idleMinutes = server.Config().idleTimeoutMinutes;
        if (idleMinutes > 0 && state == State::InGame && !server.Config().loopbackOnly &&
            now - lastActive > std::chrono::minutes(idleMinutes)) {
            LogInfo(name + " kicked: idle for " + std::to_string(idleMinutes) + " minutes");
            Kick("disconnectionScreen.idleTimeout");
            return;
        }
        if (state == State::Loading || state == State::InGame)
            StreamChunks();
        if (state == State::InGame && now - lastHeartbeat > kHeartbeat) {
            lastHeartbeat = now;
            proto::SetTimePacket t;
            t.time = static_cast<int32_t>(server.DayTime() % 2147483647);
            Send(t);
        }
        Flush();
    }
}
