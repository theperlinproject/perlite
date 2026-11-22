#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::SendAttributes() {
        proto::UpdateAttributesPacket p;
        p.runtimeEntityId = runtimeId;
        p.tick = server.TickCount();
        const auto add = [&](const char* attribute, float min, float max, float current, float fallback) {
            proto::PlayerAttributesEntry a;
            a.name = attribute;
            a.min = a.defaultMin = min;
            a.max = a.defaultMax = max;
            a.current = current;
            a.default_ = fallback;
            p.attributes.push_back(std::move(a));
        };
        const float maxHealth = MaxHealth();
        add("minecraft:health", 0.0f, maxHealth, std::clamp(std::ceil(health), 0.0f, maxHealth), 20.0f);
        add("minecraft:player.hunger", 0.0f, 20.0f, static_cast<float>(food), 20.0f);
        add("minecraft:player.saturation", 0.0f, 20.0f, saturation, 5.0f);
        add("minecraft:player.exhaustion", 0.0f, 5.0f, std::min(exhaustion, 5.0f), 0.0f);
        add("minecraft:player.level", 0.0f, 24791.0f, static_cast<float>(xpLevel), 0.0f);
        add("minecraft:player.experience", 0.0f, 1.0f, std::clamp(xpProgress, 0.0f, 1.0f), 0.0f);
        add("minecraft:absorption", 0.0f, 16.0f, std::min(absorption, 16.0f), 0.0f);
        float speed = 0.1f;
        if (const int level = EffectLevel(1); level > 0)
            speed *= 1.0f + 0.2f * static_cast<float>(level);
        if (const int level = EffectLevel(2); level > 0)
            speed *= std::max(0.0f, 1.0f - 0.15f * static_cast<float>(level));
        add("minecraft:movement", 0.0f, 3.4028235e38f, speed, 0.1f);
        Send(p);
    }

    void Session::SendAbilities() {
        namespace F = proto::FlagsAbilitySet;
        uint32_t enabled = 0;
        if (!isVisitor && gameMode != 6) {
            if (gameMode != 2)
                enabled |= F::Build | F::Mine;
            enabled |= F::DoorsAndSwitches | F::OpenContainers | F::AttackPlayers | F::AttackMobs;
        }
        if (gameMode == 1)
            enabled |= F::InstantBuild | F::MayFly | F::Invulnerable;
        if (gameMode == 6)
            enabled |= F::MayFly | F::Flying | F::Invulnerable | F::NoClip;
        if (isOperator)
            enabled |= F::OperatorCommands;
        proto::UpdateAbilitiesPacket p;
        p.entityUniqueId = static_cast<int64_t>(runtimeId);
        p.permissionLevel = isOperator  ? proto::PermissionLevel::Operator
                            : isVisitor ? proto::PermissionLevel::Visitor
                                        : proto::PermissionLevel::Member;
        p.commandPermission =
            isOperator ? proto::CommandPermissionLevel::Operator : proto::CommandPermissionLevel::Normal;
        proto::AbilityLayers layer;
        layer.type = proto::AbilityLayersType::Base;
        layer.allowed = F::Count - 1;
        layer.enabled = enabled;
        const float speed = gameMode == 6 ? static_cast<float>(flySpeedLevel) : 1.0f;
        layer.flySpeed = 0.05f * speed;
        layer.verticalFlySpeed = 1.0f * speed;
        layer.walkSpeed = 0.1f;
        p.abilities.push_back(layer);
        Send(p);
    }

    bool Session::SetFlySpeedLevel(int level) {
        if (gameMode != 6 || (level != 1 && level != 2 && level != 5 && level != 10 && level != 20))
            return false;
        flySpeedLevel = level;
        if (state == State::InGame)
            SendAbilities();
        return true;
    }

    void Session::SetGameMode(int mode) {
        if (mode != 0 && mode != 1 && mode != 2 && mode != 6)
            return;
        if (mode == 6 && gameMode != 6)
            flySpeedLevel = 1;
        const int old = gameMode;
        gameMode = mode;
        creative = mode == 1;
        if (state != State::InGame)
            return;
        if (old != mode)
            server.HookGameModeChange(*this, old, mode);
        proto::SetPlayerGameTypePacket p;
        p.gamemode = static_cast<proto::GameMode>(mode);
        Send(p);
        SendAbilities();
    }

    void Session::SetOperator(bool op) {
        if (isOperator == op)
            return;
        isOperator = op;
        if (op)
            isVisitor = false;
        if (state != State::InGame)
            return;
        SendAbilities();
        SendCommands();
    }

    void Session::SendSelfData() {
        namespace F = proto::FlagsMetadataFlags1;
        uint64_t flags = F::HasCollision | F::AffectedByGravity | F::CanClimb | F::Walker;
        if (air >= 300 || EffectLevel(13) > 0)
            flags |= F::Breathing;
        if (fireTicks > 0 && EffectLevel(12) == 0)
            flags |= F::Onfire;
        if (sneaking)
            flags |= F::Sneaking;
        if (shieldUp || useTicks >= 0)
            flags |= F::Action;
        if (sprinting)
            flags |= F::Sprinting;
        if (swimming)
            flags |= F::Swimming;
        if (gliding)
            flags |= F::Gliding;
        std::vector<proto::MetadataDictionaryEntry> meta;
        proto::MetadataDictionaryEntry f;
        f.key = proto::MetadataDictionaryEntryKey::Flags;
        f.type = proto::MetadataDictionaryEntryType::Long;
        f.legacyType = static_cast<uint8_t>(f.type); // DataItem Type byte, = index
        f.value.template emplace<1>(static_cast<int64_t>(flags));
        meta.push_back(f);
        proto::MetadataDictionaryEntry a;
        a.key = proto::MetadataDictionaryEntryKey::Air;
        a.type = proto::MetadataDictionaryEntryType::Short;
        a.legacyType = static_cast<uint8_t>(a.type); // DataItem Type byte, = index
        a.value.template emplace<4>().template emplace<2>(static_cast<int16_t>(std::clamp(air, -20, 300)));
        meta.push_back(a);
        proto::MetadataDictionaryEntry m;
        m.key = proto::MetadataDictionaryEntryKey::MaxAirdataMaxAir;
        m.type = proto::MetadataDictionaryEntryType::Short;
        m.legacyType = static_cast<uint8_t>(m.type); // DataItem Type byte, = index
        m.value.template emplace<4>().template emplace<2>(static_cast<int16_t>(300));
        meta.push_back(m);
        proto::MetadataDictionaryEntry flags2;
        flags2.key = proto::MetadataDictionaryEntryKey::FlagsExtended;
        flags2.type = proto::MetadataDictionaryEntryType::Long;
        flags2.legacyType = static_cast<uint8_t>(flags2.type); // DataItem Type byte, = index
        // Bedrock numbers actor flags as one list; flags 64 and up live in the extended field at
        // bit (flag - 64). Sleeping is flag 76. (The spec's second flag list drops laying_down, flag
        // 64, so its generated constants sit one bit low.) The extended field is variant slot 2.
        constexpr uint64_t kSleepingExtended = 1ull << (76 - 64);
        flags2.value.template emplace<2>(static_cast<int64_t>(sleeping ? kSleepingExtended : 0ull));
        meta.push_back(flags2);
        proto::MetadataDictionaryEntry pf;
        pf.key = proto::MetadataDictionaryEntryKey::PlayerFlags;
        pf.type = proto::MetadataDictionaryEntryType::Byte;
        pf.legacyType = static_cast<uint8_t>(pf.type); // DataItem Type byte, = index
        pf.value.template emplace<4>().template emplace<1>(static_cast<int8_t>(sleeping ? 2 : 0));
        meta.push_back(pf);
        proto::MetadataDictionaryEntry bed;
        bed.key = proto::MetadataDictionaryEntryKey::PlayerBedPosition;
        bed.type = proto::MetadataDictionaryEntryType::Vec3I;
        bed.legacyType = static_cast<uint8_t>(bed.type); // DataItem Type byte, = index
        bed.value.template emplace<4>().template emplace<7>(
            proto::Vec3I{sleeping ? bedX : 0, sleeping ? bedY : 0, sleeping ? bedZ : 0});
        meta.push_back(bed);
        proto::SetEntityDataPacket p;
        p.runtimeEntityId = runtimeId;
        p.metadata = meta;
        p.tick = server.TickCount();
        for (Session* s : server.PlayersIn(dimension))
            s->Send(p);
    }

    bool Session::StartSleeping(int x, int y, int z) {
        if (sleeping || dead || gameMode == 6 || dimension != kOverworld)
            return false;
        const int64_t time = server.DayTime() % 24000;
        if (!(time >= 12542 && time <= 23459) && !server.Thundering()) {
            SendActionBar("You can only sleep at night or during thunderstorms");
            SetSpawnPoint(x, y + 1, z);
            return false;
        }
        const double dx = posX - (x + 0.5), dz = posZ - (z + 0.5);
        if (dx * dx + dz * dz > 9.0) {
            SendActionBar("You may not rest now; the bed is too far away");
            return false;
        }
        sleeping = true;
        bedX = x;
        bedY = y;
        bedZ = z;
        SetSpawnPoint(x, y + 1, z);
        SendSelfData();
        server.Broadcast(name + " is sleeping in a bed");
        return true;
    }

    void Session::WakeUp() {
        if (!sleeping)
            return;
        sleeping = false;
        SendSelfData();
        proto::AnimatePacket wake;
        wake.actionId = proto::AnimatePacketActionId::WakeUp;
        wake.runtimeEntityId = runtimeId;
        for (Session* s : server.PlayersIn(dimension))
            s->Send(wake);
    }
}
