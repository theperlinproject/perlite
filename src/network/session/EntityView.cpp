#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::ShowItem(const ItemEntity& item) {
        proto::AddItemEntityPacket p;
        p.entityIdSelf = static_cast<int64_t>(item.id);
        p.runtimeEntityId = item.id;
        p.item = ToWire(item.stack);
        p.position = {static_cast<float>(item.x), static_cast<float>(item.y), static_cast<float>(item.z)};
        p.velocity = {static_cast<float>(item.vx), static_cast<float>(item.vy), static_cast<float>(item.vz)};
        Send(p);
    }

    void Session::HideItem(uint64_t id) {
        proto::RemoveEntityPacket p;
        p.entityIdSelf = static_cast<int64_t>(id);
        Send(p);
    }

    void Session::MoveItem(const ItemEntity& item) {
        proto::MoveEntityPacket p;
        p.runtimeEntityId = item.id;
        p.flags = item.onGround ? 1 : 0;
        p.position = {static_cast<float>(item.x), static_cast<float>(item.y), static_cast<float>(item.z)};
        Send(p);
    }

    void Session::TakeItem(uint64_t itemId, uint64_t takerRuntimeId) {
        proto::TakeItemEntityPacket p;
        p.runtimeEntityId = takerRuntimeId;
        p.target = itemId;
        Send(p);
    }

    void Session::ShowMob(const Mob& mob) {
        proto::AddEntityPacket p;
        p.uniqueId = static_cast<int64_t>(mob.id);
        p.runtimeId = mob.id;
        p.entityType = mob.def->id;
        p.position = {static_cast<float>(mob.x), static_cast<float>(mob.y), static_cast<float>(mob.z)};
        p.velocity = {static_cast<float>(mob.vx), static_cast<float>(mob.vy), static_cast<float>(mob.vz)};
        p.pitch = mob.pitch;
        p.yaw = mob.yaw;
        p.headYaw = mob.headYaw;
        p.bodyYaw = mob.yaw;
        proto::EntityAttributesEntry hp;
        hp.name = "minecraft:health";
        hp.min = 0;
        hp.value = mob.health;
        hp.max = mob.maxHealth > 0 ? mob.maxHealth : mob.def->health;
        p.attributes.push_back(hp);
        proto::EntityAttributesEntry move;
        move.name = "minecraft:movement";
        move.min = 0;
        move.value = mob.def->speed;
        move.max = 3.4028235e38f;
        p.attributes.push_back(move);
        p.metadata = MobMetadata(mob);
        Send(p);
    }

    void Session::MoveMob(const Mob& mob) {
        proto::MoveEntityPacket p;
        p.runtimeEntityId = mob.id;
        p.flags = mob.onGround ? 1 : 0;
        p.position = {static_cast<float>(mob.x), static_cast<float>(mob.y), static_cast<float>(mob.z)};
        p.rotation.pitch = MobAngle(mob.pitch);
        p.rotation.yaw = MobAngle(mob.yaw);
        p.rotation.headYaw = MobAngle(mob.headYaw);
        Send(p);
    }

    void Session::MobData(const Mob& mob) {
        proto::SetEntityDataPacket p;
        p.runtimeEntityId = mob.id;
        p.metadata = MobMetadata(mob);
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::MobMotion(const Mob& mob) {
        proto::SetEntityMotionPacket p;
        p.runtimeEntityId = mob.id;
        p.velocity = {static_cast<float>(mob.vx), static_cast<float>(mob.vy), static_cast<float>(mob.vz)};
        p.tick = server.TickCount();
        Send(p);
    }

    void Session::CritAnimation(uint64_t id) {
        proto::AnimatePacket p;
        p.actionId = proto::AnimatePacketActionId::CriticalHit;
        p.runtimeEntityId = id;
        Send(p);
    }

    void Session::MobEvent(uint64_t id, int event, int data) {
        proto::EntityEventPacket p;
        p.runtimeEntityId = id;
        p.eventId = static_cast<proto::EntityEventPacketEventId>(event);
        p.data = data;
        Send(p);
    }
}
