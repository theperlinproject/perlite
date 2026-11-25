#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::OnRespawn(std::span<const uint8_t> body) {
        proto::RespawnPacket p;
        if (!Decode(body, p) || state != State::InGame)
            return;
        const bool wasDead = dead;
        if (dead) {
            dead = false;
            health = MaxHealth();
            food = 20;
            saturation = 5.0f;
            exhaustion = 0;
            foodTimer = 0;
            absorption = 0;
            fireTicks = 0;
            air = 300;
            fallDistance = 0;
            invulnerable = 60;
            dataDirty = true;
            // Hardcore has one life: the player stays where they died and watches as a spectator.
            if (!server.Config().hardcore) {
                posX = (hasSpawnPoint ? spawnPointX : server.SpawnX()) + 0.5;
                posY = (hasSpawnPoint ? spawnPointY : server.SpawnY()) + 1.62;
                posZ = (hasSpawnPoint ? spawnPointZ : server.SpawnZ()) + 0.5;
            }
            SendAttributes();
        }
        proto::RespawnPacket reply;
        reply.position = {static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(posZ)};
        reply.state = 1;
        reply.runtimeEntityId = runtimeId;
        Send(reply);
        if (wasDead && server.Config().hardcore) {
            SetGameMode(6);
            server.HookPlayerRespawn(*this);
            return;
        }
        if (wasDead && dimension != kOverworld)
            server.ChangeDimension(*this, kOverworld, posX, posY, posZ);
        else if (wasDead)
            Teleport(posX, posY, posZ);
        if (wasDead)
            server.HookPlayerRespawn(*this);
    }

    void Session::SetPosition(double x, double y, double z, float p, float yawValue) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return;
        if (std::abs(x) > 3.0e7 || std::abs(z) > 3.0e7 || std::abs(y) > 1.0e5)
            return;
        if (awaitingDimensionAck) {
            if (std::chrono::steady_clock::now() - dimensionChangedAt < std::chrono::seconds(15))
                return;
            awaitingDimensionAck = false;
        }
        const bool changed = x != posX || y != posY || z != posZ || p != pitch || yawValue != yaw;
        if (changed)
            lastActive = std::chrono::steady_clock::now();
        moved = moved || changed;
        posX = x;
        posY = y;
        posZ = z;
        pitch = p;
        yaw = yawValue;
        if (state == State::InGame || state == State::Loading)
            PublishCenter(false);
    }

    void Session::OnAuthInput(std::span<const uint8_t> body) {
        proto::PlayerAuthInputPacket in;
        if (!Decode(body, in) || (state != State::InGame && state != State::Loading))
            return;
        inputTick = in.tick;
        const double previousY = posY;
        SetPosition(in.position.x, in.position.y, in.position.z, in.pitch, in.yaw);
        const bool wasSneaking = sneaking;
        sneaking = std::ranges::find(in.inputData, proto::InputData::Sneaking) != in.inputData.end();
        if (wasSneaking != sneaking)
            dataDirty = true;
        if (state != State::InGame)
            return;
        if (std::ranges::find(in.inputData, proto::InputData::SwapOffhand) != in.inputData.end())
            SwapOffhand();
        if (std::abs(posX - lastHookX) + std::abs(posY - lastHookY) + std::abs(posZ - lastHookZ) > 0.1) {
            lastHookX = posX;
            lastHookY = posY;
            lastHookZ = posZ;
            server.HookPlayerMove(*this);
        }
        if (!awaitingDimensionAck)
            MovementTick(in, previousY);
        HandleBlockActions(in);
    }

    void Session::OnMovePlayer(std::span<const uint8_t> body) {
        proto::MovePlayerPacket m;
        if (!Decode(body, m) || (state != State::InGame && state != State::Loading))
            return;
        SetPosition(m.position.x, m.position.y, m.position.z, m.pitch, m.yaw);
    }

    void Session::Teleport(double x, double y, double z, float yawDegrees, float pitchDegrees) {
        yaw = yawDegrees;
        pitch = pitchDegrees;
        Teleport(x, y, z);
    }

    void Session::Teleport(double x, double y, double z) {
        posX = x;
        posY = y;
        posZ = z;
        proto::MovePlayerPacket m;
        m.runtimeId = runtimeId;
        m.position = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        m.pitch = pitch;
        m.yaw = yaw;
        m.headYaw = yaw;
        m.mode = proto::MovePlayerPacketMode::Teleport;
        m.teleport = proto::MovePlayerPacketTeleport{};
        Send(m);
        PublishCenter(false);
    }

    void Session::SendMove(const Session& other, uint8_t mode) {
        proto::MovePlayerPacket m;
        m.runtimeId = other.runtimeId;
        m.position = {static_cast<float>(other.posX), static_cast<float>(other.posY),
                      static_cast<float>(other.posZ)};
        m.pitch = other.pitch;
        m.yaw = other.yaw;
        m.headYaw = other.yaw;
        m.mode = static_cast<proto::MovePlayerPacketMode>(mode);
        Send(m);
    }

    void Session::MovementTick(const proto::PlayerAuthInputPacket& in, double previousY) {
        const auto has = [&](proto::InputData d) {
            return std::ranges::find(in.inputData, d) != in.inputData.end();
        };
        const bool wasSprinting = sprinting, wasSwimming = swimming, wasGliding = gliding;
        if (has(proto::InputData::StartSprinting))
            sprinting = true;
        if (has(proto::InputData::StopSprinting))
            sprinting = false;
        if (has(proto::InputData::Sprinting))
            sprinting = true;
        else if (!has(proto::InputData::StartSprinting) && sprinting && in.moveVector.z <= 0.0f)
            sprinting = false;
        if (has(proto::InputData::StartSwimming))
            swimming = true;
        if (has(proto::InputData::StopSwimming))
            swimming = false;
        if (has(proto::InputData::StartGliding))
            gliding = true;
        if (has(proto::InputData::StopGliding))
            gliding = false;
        if (has(proto::InputData::StartFlying))
            flying = true;
        if (has(proto::InputData::StopFlying))
            flying = false;
        if (gameMode != 1 && gameMode != 6 && flying && !(gameMode == 1))
            flying = false;
        if (sprinting && food <= 6 && gameMode != 1)
            sprinting = false;
        if (wasSprinting != sprinting || wasSwimming != swimming || wasGliding != gliding)
            dataDirty = true;
        if (gameMode == 1 || gameMode == 6 || dead) {
            fallDistance = 0;
            onGround = true;
            return;
        }
        const double dy = posY - previousY;
        const bool collided = has(proto::InputData::VerticalCollision);
        const std::string feet = BlockNameAt(posX, posY - 1.62 + 0.1, posZ);
        const std::string under = BlockNameAt(posX, posY - 1.62 - 0.2, posZ);
        const bool inWater = feet.contains("water") || feet.contains("bubble_column");
        const bool climbing = feet.contains("ladder") || feet.contains("vine") || feet.contains("scaffolding") ||
                              feet.contains("web") || feet.contains("powder_snow") || feet.contains("sweet_berry");
        const bool grounded = collided && dy <= 0.0001;
        if (has(proto::InputData::StartJumping)) {
            Exhaust(sprinting ? 0.2f : 0.05f);
            jumping = true;
        }
        const double mx = posX - lastMoveX, mz = posZ - lastMoveZ;
        lastMoveX = posX;
        lastMoveZ = posZ;
        const double horizontal = std::sqrt(mx * mx + mz * mz);
        if (horizontal < 2.0) {
            if (swimming || inWater)
                Exhaust(static_cast<float>(0.01 * horizontal));
            else if (sprinting && onGround)
                Exhaust(static_cast<float>(0.1 * horizontal));
        }
        if (inWater || climbing || flying || EffectLevel(27) > 0 || EffectLevel(24) > 0 || feet.contains("lava")) {
            fallDistance = 0;
        } else if (gliding) {
            if (dy > -0.5)
                fallDistance = 0;
        } else if (dy < 0 && !grounded) {
            fallDistance -= static_cast<float>(dy);
        }
        if (grounded && !onGround) {
            float damage = std::ceil(fallDistance - 3.0f - static_cast<float>(EffectLevel(8)) - 0.001f);
            const std::string land = under.empty() ? feet : under;
            if (land.contains("hay_block") || land.contains("honey_block"))
                damage = std::ceil(damage * 0.2f);
            if (land.contains("slime") || land.contains("_bed") || land == "minecraft:bed")
                damage = land.contains("slime") && !sneaking ? 0.0f : std::ceil(damage * 0.5f);
            if (land.contains("powder_snow") || land.contains("water") || land.contains("cobweb") || land.contains(":web"))
                damage = 0;
            if (land.contains("pointed_dripstone") && fallDistance > 2.0f)
                damage = std::ceil((fallDistance - 2.0f) * 2.0f);
            if (damage > 0)
                Damage(damage, DamageCause::Fall, posX, posZ);
            fallDistance = 0;
            jumping = false;
        } else if (grounded) {
            fallDistance = 0;
        }
        onGround = grounded;
    }

    void Session::SetVelocity(double x, double y, double z) {
        proto::SetEntityMotionPacket p;
        p.runtimeEntityId = runtimeId;
        p.velocity = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        p.tick = inputTick;
        Send(p);
    }

    void Session::SetSpawnPoint(int x, int y, int z) {
        hasSpawnPoint = true;
        spawnPointX = x;
        spawnPointY = y;
        spawnPointZ = z;
        if (state == State::InGame)
            SendSpawnPosition(x, y, z, false);
    }
}
