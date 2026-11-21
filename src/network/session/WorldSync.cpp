#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::SendTime(int32_t ticks) {
        proto::SetTimePacket t;
        t.time = ticks;
        Send(t);
    }

    void Session::SendWeather(bool rain, bool thunder) {
        const auto event = [&](proto::LevelEventPacketEvent e, int data) {
            proto::LevelEventPacket p;
            p.event = e;
            p.position = {0, 0, 0};
            p.data = data;
            Send(p);
        };
        event(rain ? proto::LevelEventPacketEvent::StartRain : proto::LevelEventPacketEvent::StopRain,
              rain ? 65535 : 0);
        event(thunder ? proto::LevelEventPacketEvent::StartThunder
                      : proto::LevelEventPacketEvent::StopThunder,
              thunder ? 65535 : 0);
    }

    void Session::SendGameRule(const std::string& lowerName, bool boolean, int value) {
        proto::GameRulesChangedPacket p;
        proto::GameRuleI32 r;
        r.name = lowerName;
        r.editable = true;
        if (boolean) {
            r.type = proto::GameRuleI32Type::Bool;
            r.value = value != 0;
        } else {
            r.type = proto::GameRuleI32Type::Int;
            r.value = static_cast<int32_t>(value);
        }
        p.rules.push_back(std::move(r));
        Send(p);
    }

    void Session::SendSpawnPosition(int x, int y, int z, bool world) {
        proto::SetSpawnPositionPacket p;
        p.spawnType = world ? proto::SetSpawnPositionPacketSpawnType::World
                            : proto::SetSpawnPositionPacketSpawnType::Player;
        p.playerPosition = {x, y, z};
        p.dimension = 0;
        p.worldPosition = {x, y, z};
        Send(p);
    }

    void Session::SendDifficulty(int difficulty) {
        proto::SetDifficultyPacket p;
        p.difficulty = static_cast<uint32_t>(difficulty);
        Send(p);
    }

    void Session::CorrectBlock(int x, int y, int z) {
        server.SendBlockTo(*this, x, y, z);
    }

    void Session::PlaySound(const std::string& sound, double x, double y, double z, int extra) {
        proto::LevelSoundEventPacket p;
        p.soundId = sound;
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.extraData = extra;
        p.entityType = ":";
        p.entityUniqueId = -1;
        Send(p);
    }

    void Session::SendLevelEvent(int event, double x, double y, double z, int data) {
        proto::LevelEventPacket p;
        p.event = static_cast<proto::LevelEventPacketEvent>(event);
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.data = data;
        Send(p);
    }

    void Session::SendBlockUpdate(int x, int y, int z, int32_t networkId) {
        proto::UpdateBlockPacket u;
        u.position = {x, y, z};
        u.blockRuntimeId = static_cast<uint32_t>(networkId);
        u.flags = 3;
        u.layer = 0;
        Send(u);
    }

    void Session::PlaySoundTo(const std::string& sound, double x, double y, double z, float volume, float pitchValue) {
        proto::PlaySoundPacket p;
        p.name = sound;
        p.coordinates = {static_cast<int32_t>(x * 8), static_cast<int32_t>(y * 8), static_cast<int32_t>(z * 8)};
        p.volume = volume;
        p.pitch = pitchValue;
        Send(p);
    }

    void Session::SpawnParticle(const std::string& particle, double x, double y, double z) {
        proto::SpawnParticleEffectPacket p;
        p.dimension = static_cast<uint8_t>(dimension);
        p.entityId = -1;
        p.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.particleName = particle;
        Send(p);
    }

    void Session::StopSound(const std::string& sound) {
        proto::StopSoundPacket p;
        p.name = sound;
        p.stopAll = sound.empty();
        Send(p);
    }
}
