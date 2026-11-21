#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::OnAnimate(std::span<const uint8_t> body) {
        proto::AnimatePacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        p.runtimeEntityId = runtimeId;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnPlayerSkin(std::span<const uint8_t> body) {
        proto::PlayerSkinPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        *skin = p.skin;
        p.uuid.bytes = uuid;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnTickSync(std::span<const uint8_t> body) {
        proto::TickSyncPacket p;
        if (!Decode(body, p))
            return;
        p.responseTime = static_cast<int64_t>(server.TickCount());
        Send(p);
    }

    void Session::OnLatency(std::span<const uint8_t> body) {
        proto::NetworkStackLatencyPacket p;
        if (!Decode(body, p) || !p.needsResponse)
            return;
        p.needsResponse = 0;
        Send(p);
    }

    void Session::OnEmote(std::span<const uint8_t> body) {
        proto::EmotePacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        p.entityId = runtimeId;
        p.xuid = xuid;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::OnSound(std::span<const uint8_t> body) {
        proto::LevelSoundEventPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        for (Session* other : server.InGamePlayers())
            if (other != this)
                other->Send(p);
    }

    void Session::ShowArmor(const Session& wearer) {
        proto::MobArmorEquipmentPacket p;
        p.runtimeEntityId = wearer.runtimeId;
        p.helmet = ToWire(wearer.inventory.armor[0]);
        p.chestplate = ToWire(wearer.inventory.armor[1]);
        p.leggings = ToWire(wearer.inventory.armor[2]);
        p.boots = ToWire(wearer.inventory.armor[3]);
        Send(p);
    }

    void Session::ShowPlayer(const Session& other) {
        proto::PlayerListPacket list;
        proto::PlayerRecord rec;
        rec.type = proto::PlayerRecordType::Add;
        rec.uuid.bytes = other.uuid;
        rec.entityUniqueId = static_cast<int64_t>(other.runtimeId);
        rec.username = other.name;
        rec.xboxUserId = other.xuid;
        rec.skinData = other.GetSkin();
        rec.playerColor = -1;
        list.records.push_back(std::move(rec));
        Send(list);

        proto::AddPlayerPacket add;
        add.uuid.bytes = other.uuid;
        add.username = other.name;
        add.runtimeId = other.runtimeId;
        add.position = {static_cast<float>(other.posX), static_cast<float>(other.posY),
                        static_cast<float>(other.posZ)};
        add.pitch = other.pitch;
        add.yaw = other.yaw;
        add.headYaw = other.yaw;
        add.gamemode = static_cast<proto::GameMode>(other.gameMode);
        add.uniqueId = static_cast<int64_t>(other.runtimeId);
        add.permissionLevel = other.isOperator  ? proto::PermissionLevel::Operator
                              : other.isVisitor ? proto::PermissionLevel::Visitor
                                                : proto::PermissionLevel::Member;
        add.commandPermission = other.isOperator ? proto::CommandPermissionLevel::Operator
                                                 : proto::CommandPermissionLevel::Normal;
        Send(add);
    }

    void Session::HidePlayer(const Session& other) {
        proto::RemoveEntityPacket r;
        r.entityIdSelf = static_cast<int64_t>(other.runtimeId);
        Send(r);
        proto::PlayerListPacket list;
        proto::PlayerRecord rec;
        rec.type = proto::PlayerRecordType::Remove;
        rec.uuid.bytes = other.uuid;
        list.records.push_back(std::move(rec));
        Send(list);
    }
}
