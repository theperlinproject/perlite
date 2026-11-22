#include "network/session/SessionInternal.hpp"
#include "world/level/block/VoxelShapes.hpp"
#include "server/ServerData.hpp"
#include "server/vanilla/BedrockData.hpp"

namespace perlite {
    void Session::OnRequestSettings(std::span<const uint8_t> body) {
        proto::RequestNetworkSettingsPacket req;
        if (state != State::AwaitSettings || !Decode(body, req)) {
            Close();
            return;
        }
        if (req.clientProtocol != kProtocol) {
            proto::PlayStatusPacket p;
            p.status = req.clientProtocol < kProtocol ? proto::PlayStatusPacketStatus::FailedClient
                                                      : proto::PlayStatusPacketStatus::FailedSpawn;
            std::vector<uint8_t> out;
            net::EncodeChecked(p, out);
            SendImmediate(proto::PlayStatusPacket::kId, out);
            LogInfo(connection->Describe() + ": protocol " + std::to_string(req.clientProtocol) +
                    " rejected (server speaks " + std::to_string(kProtocol) + ")");
            Close();
            return;
        }
        proto::NetworkSettingsPacket settings;
        settings.compressionThreshold = server.Config().compressionThreshold;
        settings.compressionAlgorithm = proto::NetworkSettingsPacketCompressionAlgorithm::Deflate;
        std::vector<uint8_t> out;
        net::EncodeChecked(settings, out);
        SendImmediate(proto::NetworkSettingsPacket::kId, out);
        codec.EnableCompression(server.Config().compressionThreshold);
        state = State::AwaitLogin;
        stateSince = std::chrono::steady_clock::now();
    }

    void Session::OnLogin(std::span<const uint8_t> body) {
        proto::LoginPacket login;
        if (state != State::AwaitLogin || !Decode(body, login)) {
            Close();
            return;
        }
        const auth::Login login2 = auth::VerifyLogin(
            login.tokens.identity, login.tokens.client,
            server.Config().authRootKeys.empty() ? auth::DefaultRootKeys() : server.Config().authRootKeys);
        if (!login2.chainValid) {
            LogWarn(connection->Describe() + ": login rejected: " + login2.error);
            Kick("Invalid login");
            return;
        }
        if (server.Config().xboxAuth) {
            std::string why;
            if (!login2.xboxVerified) {
                why = "not signed in to Xbox Live";
            } else if (!connection->offerSdp.empty() &&
                       !auth::VerifyOfferBinding(connection->offerSdp, login2.clientKey, why)) {
            } else {
                why.clear();
            }
            if (!why.empty()) {
                LogInfo(connection->Describe() + ": refused: " + why);
                proto::DisconnectPacket d;
                d.reason = proto::DisconnectFailReason::NotAuthenticated;
                d.message = "This server requires you to be signed in to Xbox Live";
                d.filteredMessage = d.message;
                Send(d);
                Flush();
                Close();
                return;
            }
        }
        name = login2.name;
        xuid = login2.xuid;
        std::string uuidText = login2.uuid;
        const json::Value clientData = login2.clientData;
        const bool haveClient = login2.clientDataValid;
        if (name.empty() && haveClient)
            name = clientData["ThirdPartyName"].String();
        if (name.empty()) {
            Kick("Invalid login: no player name");
            return;
        }
        for (char& c : name)
            if (static_cast<unsigned char>(c) < 0x20)
                c = ' ';
        if (name.size() > 32)
            name.resize(32);
        uuid = ParseUuid(uuidText, runtimeId);
        *skin = SkinFromClientData(haveClient ? clientData : json::Value{}, name);
        {
            // Mojang's BuildPlatform: Google 1, iOS 2, OSX 3, Amazon 4, Win32 8, Dedicated 9,
            // Sony 11, Nintendo 12, Xbox 13, Linux 15, Unknown -1. Anything else is rejected.
            const int64_t os = haveClient ? clientData["DeviceOS"].Int(-1) : -1;
            static constexpr int32_t kValid[] = {1, 2, 3, 4, 8, 9, 11, 12, 13, 15, -1};
            buildPlatform = -1;
            for (int32_t v : kValid)
                if (os == v)
                    buildPlatform = v;
        }
        isOperator = server.IsOperator(name, xuid);
        isVisitor =
            !server.Config().loopbackOnly && !isOperator && server.PermissionOf(name, xuid) == "visitor";
        gameMode = server.Config().gameMode;
        creative = gameMode == 1;

        if (server.Config().whitelist && !server.Config().loopbackOnly && !server.IsWhitelisted(name, xuid)) {
            LogInfo(name + " refused: not on the allowlist");
            Kick("disconnectionScreen.notAllowed");
            return;
        }
        if (!server.Config().loopbackOnly && server.IpBans().Contains(connection->RemoteIp())) {
            LogInfo(name + " refused: address " + connection->RemoteIp() + " is banned");
            Kick("You are banned from this server.");
            return;
        }
        if (server.IsBanned(name, xuid) && !server.Config().loopbackOnly) {
            LogInfo(name + " refused: banned");
            Kick("You are banned from this server.");
            return;
        }
        if (std::string reason; server.HookPreLogin(*this, reason)) {
            LogInfo(name + " refused by a plugin");
            Kick(reason.empty() ? "disconnectionScreen.noReason" : reason);
            return;
        }
        if (server.Full() && !server.IgnoresPlayerLimit(name, xuid)) {
            proto::PlayStatusPacket p;
            p.status = proto::PlayStatusPacketStatus::FailedServerFull;
            Send(p);
            Flush();
            Close();
            return;
        }
        if (Session* other = server.FindByName(name); other && other != this && other->InGame()) {
            Kick("A player with this name is already connected");
            return;
        }

        clientKey = login2.clientKey;
        LogInfo("UUID of player " + name + " is " + uuidText);
        if (server.Config().networkEncryption && (!connection->Local() || server.Config().encryptLocal) &&
            !clientKey.empty()) {
            std::string token, why;
            if (!cipher.PrepareServerHandshake(clientKey, token, why)) {
                LogWarn(name + ": encryption handshake failed: " + why);
                Kick("disconnectionScreen.notAuthenticated");
                return;
            }
            Flush();
            proto::ServerToClientHandshakePacket handshake;
            handshake.token = token;
            std::vector<uint8_t> out;
            net::EncodeChecked(handshake, out);
            SendImmediate(proto::ServerToClientHandshakePacket::kId, out);
            if (!connection->SecureTransport())
                cipher.Activate();
            state = State::AwaitHandshake;
            stateSince = std::chrono::steady_clock::now();
            return;
        }
        CompleteLogin();
    }

    void Session::OnClientHandshake() {
        if (state != State::AwaitHandshake) {
            Kick("disconnectionScreen.unexpectedPacket");
            return;
        }
        CompleteLogin();
    }

    void Session::CompleteLogin() {
        server.LoadPlayer(*this);
        proto::PlayStatusPacket ok;
        ok.status = proto::PlayStatusPacketStatus::LoginSuccess;
        Send(ok);
        SendResourcePacks();
        state = State::ResourcePacks;
        stateSince = std::chrono::steady_clock::now();
    }

    void Session::SendResourcePacks() {
        proto::ResourcePacksInfoPacket info;
        server.Packs().FillInfo(info);
        Send(info);
    }

    void Session::OnPackResponse(std::span<const uint8_t> body) {
        proto::ResourcePackClientResponsePacket r;
        if (!Decode(body, r)) {
            Close();
            return;
        }
        using Status = proto::ResourcePackClientResponsePacketResponseStatus;
        switch (r.responseStatus) {
        case Status::Refused:
            if (server.Packs().Required() && !server.Packs().Packs().empty()) {
                Kick("disconnectionScreen.noReason");
                break;
            }
            {
                proto::ResourcePackStackPacket stack;
                stack.gameVersion = "*";
                Send(stack);
                state = State::ResourceStack;
            }
            break;
        case Status::SendPacks: {
            bool any = false;
            if (r.resourcepackids)
                for (const std::string& id : *r.resourcepackids) {
                    proto::ResourcePackDataInfoPacket info;
                    if (!server.Packs().DataInfo(id, info)) {
                        Kick("disconnectionScreen.resourcePack");
                        return;
                    }
                    Send(info);
                    any = true;
                }
            if (!any)
                Kick("disconnectionScreen.resourcePack");
            break;
        }
        case Status::HaveAllPacks: {
            proto::ResourcePackStackPacket stack;
            server.Packs().FillStack(stack);
            Send(stack);
            state = State::ResourceStack;
            break;
        }
        case Status::Completed:
            if (state == State::ResourceStack || state == State::ResourcePacks) {
                SendStartGame();
                SendWorldSetup();
                state = State::Loading;
                stateSince = std::chrono::steady_clock::now();
            }
            break;
        }
    }

    void Session::OnPackChunkRequest(std::span<const uint8_t> body) {
        proto::ResourcePackChunkRequestPacket r;
        if (!Decode(body, r) || (state != State::ResourcePacks && state != State::ResourceStack))
            return;
        proto::ResourcePackChunkDataPacket chunk;
        if (!server.Packs().Chunk(r.packId, r.chunkIndex, chunk)) {
            Kick("disconnectionScreen.resourcePack");
            return;
        }
        Send(chunk);
        Flush();
    }

    void Session::SendStartGame() {
        {
            proto::JigsawStructureDataPacket jigsaw;
            jigsaw.structureData.bytes = serverdata::JigsawStructureDataNbt();
            Send(jigsaw);
        }
        Send(VoxelShapes::Instance().Packet());
        const ServerConfig& cfg = server.Config();
        proto::StartGamePacket g;
        g.entityId = static_cast<int64_t>(runtimeId);
        g.runtimeEntityId = runtimeId;
        g.playerGamemode = static_cast<proto::GameMode>(gameMode);
        g.playerPosition = {static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(posZ)};
        g.rotation = {0.0f, 0.0f};
        g.seed = cfg.seed;
        g.biomeType = 0;
        g.biomeName = "plains";
        g.dimension = static_cast<proto::StartGamePacketDimension>(dimension);
        g.generator = cfg.generator == "vanilla" ? 1 : 2;
        g.worldGamemode = static_cast<proto::GameMode>(cfg.gameMode);
        g.difficulty = cfg.difficulty;
        g.spawnPosition = {static_cast<int32_t>(std::floor(server.SpawnX())),
                           static_cast<int32_t>(std::floor(server.SpawnY())),
                           static_cast<int32_t>(std::floor(server.SpawnZ()))};
        g.achievementsDisabled = true;
        g.rainLevel = server.Raining() ? 1.0f : 0.0f;
        g.lightningLevel = server.Thundering() ? 1.0f : 0.0f;
        g.isMultiplayer = true;
        g.broadcastToLan = false;
        g.enableCommands = true;
        g.permissionLevel = isOperator  ? proto::PermissionLevel::Operator
                            : isVisitor ? proto::PermissionLevel::Visitor
                                        : proto::PermissionLevel::Member;
        g.serverChunkTickRange = static_cast<int32_t>(cfg.tickDistance);
        g.chatRestrictionLevel =
            cfg.chatRestriction == "Disabled"  ? proto::StartGamePacketChatRestrictionLevel::Disabled
            : cfg.chatRestriction == "Dropped" ? proto::StartGamePacketChatRestrictionLevel::Dropped
                                               : proto::StartGamePacketChatRestrictionLevel::None;
        g.gameVersion = kGameVersion;
        g.levelId = cfg.levelName;
        g.worldName = cfg.name;
        g.currentTick = static_cast<int64_t>(server.TickCount());
        g.multiplayerCorrelationId = "";
        g.rewindHistorySize = 40;
        g.serverAuthoritativeInventory = true;
        g.engine = "Perlite";
        g.blockNetworkIdsAreHashes = true;
        for (const NamedNbt& block : BedrockData::Instance().BlockProperties()) {
            proto::BlockPropertiesEntry entry;
            entry.name = block.name;
            entry.state.bytes = block.nbt;
            g.blockProperties.push_back(std::move(entry));
        }
        g.serverIdentifier = cfg.name;
        Send(g);
    }

    void Session::SendWorldSetup() {
        proto::SetTimePacket t;
        t.time = static_cast<int32_t>(server.DayTime() % 2147483647);
        Send(t);
        Queue(proto::ItemRegistryPacket::kId, server.ItemRegistryBody());
        {
            proto::AvailableEntityIdentifiersPacket ids;
            ids.nbt.bytes = serverdata::EntityIdentifiersNbt();
            Send(ids);
        }
        Send(proto::CameraPresetsPacket{});
        Send(proto::TrimDataPacket{});
        Send(serverdata::BiomeDefinitions());
        SendAttributes();
        SendAbilities();
        {
            proto::SetDifficultyPacket difficulty;
            difficulty.difficulty = static_cast<uint32_t>(server.Config().difficulty);
            Send(difficulty);
            proto::SetCommandsEnabledPacket commandsOn;
            commandsOn.enabled = true;
            Send(commandsOn);
        }
        SendInventory();
        Queue(proto::CreativeContentPacket::kId, server.CreativeContentBody());
        if (!Recipes::Instance().CraftingDataBody().empty())
            Queue(proto::CraftingDataPacket::kId, Recipes::Instance().CraftingDataBody());
        {
            proto::PlayerListPacket self;
            proto::PlayerRecord rec;
            rec.type = proto::PlayerRecordType::Add;
            rec.uuid.bytes = uuid;
            rec.entityUniqueId = static_cast<int64_t>(runtimeId);
            rec.username = name;
            rec.xboxUserId = xuid;
            rec.buildPlatform = buildPlatform;
            rec.skinData = *skin;
            rec.playerColor = -1;
            rec.isHost = false;
            self.records.push_back(std::move(rec));
            Send(self);
        }
        SendCommands();
    }

    void Session::SendCommands() {
        proto::AvailableCommandsPacket commands;
        FillAvailableCommands(commands, server.Commands(), isOperator);
        Send(commands);
    }
}
