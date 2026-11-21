#include "network/session/SessionInternal.hpp"
#include "protocol/NbtTree.hpp"
#include "world/inventory/ItemNbt.hpp"

namespace perlite {
    namespace {
        nbt::Value FloatList(std::initializer_list<float> values) {
            nbt::Value list = nbt::Value::Make(nbt::List);
            list.listType = nbt::Float;
            for (float f : values) {
                nbt::Value v = nbt::Value::Make(nbt::Float);
                v.real = f;
                list.list.push_back(v);
            }
            return list;
        }
    }

    std::string Session::StorageId() const {
        if (!xuid.empty())
            return xuid;
        return "name_" + str::Lower(name);
    }

    nbt::Value Session::SaveState() const {
        nbt::Value root = nbt::Value::Make(nbt::Compound);
        root.Set("Pos", FloatList({static_cast<float>(posX), static_cast<float>(posY - kEyeHeight),
                                   static_cast<float>(posZ)}));
        root.Set("Rotation", FloatList({yaw, pitch}));
        root.SetInt("DimensionId", dimension);
        root.SetInt("PlayerGameMode", gameMode);
        root.SetFloat("Health", health);
        root.SetInt("Food", food);
        root.SetFloat("Saturation", saturation);
        root.SetFloat("Exhaustion", exhaustion);
        root.SetInt("PlayerLevel", xpLevel);
        root.SetFloat("PlayerLevelProgress", xpProgress);
        root.SetInt("XpTotal", xpTotal);
        root.SetInt("SelectedInventorySlot", inventory.selected);
        root.SetShort("Air", static_cast<int16_t>(air));
        root.SetByte("Dead", dead ? 1 : 0);
        if (hasSpawnPoint) {
            root.SetInt("SpawnX", spawnPointX);
            root.SetInt("SpawnY", spawnPointY);
            root.SetInt("SpawnZ", spawnPointZ);
        }
        root.Set("Inventory", ItemListToNbt(inventory.main));
        root.Set("Armor", ItemListToNbt(inventory.armor));
        root.Set("Offhand", ItemListToNbt(std::span<const ItemStack>(&inventory.offhand, 1)));
        root.SetString("PerliteName", name);
        return root;
    }

    void Session::LoadState(const nbt::Value& root) {
        if (const nbt::Value* pos = root.Find("Pos"); pos && pos->list.size() == 3) {
            posX = pos->list[0].real;
            posY = pos->list[1].real + kEyeHeight;
            posZ = pos->list[2].real;
        }
        if (const nbt::Value* rot = root.Find("Rotation"); rot && rot->list.size() == 2) {
            yaw = static_cast<float>(rot->list[0].real);
            pitch = static_cast<float>(rot->list[1].real);
        }
        const int dim = static_cast<int>(root.Int("DimensionId", kOverworld));
        dimension = dim >= kOverworld && dim < kDimensionCount ? dim : kOverworld;
        if (!server.Config().forceGamemode) {
            const int mode = static_cast<int>(root.Int("PlayerGameMode", gameMode));
            if (mode == 0 || mode == 1 || mode == 2 || mode == 6) {
                gameMode = mode;
                creative = gameMode == 1;
            }
        }
        health = static_cast<float>(root.Real("Health", health));
        if (health <= 0.0f)
            health = 20.0f;
        food = static_cast<int>(root.Int("Food", food));
        saturation = static_cast<float>(root.Real("Saturation", saturation));
        exhaustion = static_cast<float>(root.Real("Exhaustion", exhaustion));
        xpLevel = static_cast<int>(root.Int("PlayerLevel", xpLevel));
        xpProgress = static_cast<float>(root.Real("PlayerLevelProgress", xpProgress));
        xpTotal = static_cast<int>(root.Int("XpTotal", xpTotal));
        inventory.selected = std::clamp(static_cast<int>(root.Int("SelectedInventorySlot", 0)), 0, 8);
        air = static_cast<int>(root.Int("Air", air));
        if (root.Find("SpawnX")) {
            hasSpawnPoint = true;
            spawnPointX = static_cast<int>(root.Int("SpawnX"));
            spawnPointY = static_cast<int>(root.Int("SpawnY"));
            spawnPointZ = static_cast<int>(root.Int("SpawnZ"));
        }
        ItemListFromNbt(root.Find("Inventory"), &inventory, inventory.main);
        ItemListFromNbt(root.Find("Armor"), &inventory, inventory.armor);
        ItemListFromNbt(root.Find("Offhand"), &inventory, std::span<ItemStack>(&inventory.offhand, 1));
    }
}
