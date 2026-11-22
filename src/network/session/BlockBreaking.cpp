#include "network/session/SessionInternal.hpp"
#include "world/loot/Loot.hpp"

#include <algorithm>
#include <cmath>

namespace perlite {
    namespace {
        constexpr int kHaste = 3;
        constexpr int kMiningFatigue = 4;
        constexpr int kConduitPower = 26;
        constexpr int kEfficiency = 15;
        constexpr int kAquaAffinity = 8;

        std::string Bare(const std::string& name) {
            const auto colon = name.find(':');
            return colon == std::string::npos ? name : name.substr(colon + 1);
        }

        // How much faster than a bare hand the held item digs this block.
        double ToolSpeed(const std::string& tool, const std::string& block, bool& correct) {
            const std::string b = Bare(block);
            correct = false;
            if (tool.empty())
                return 1.0;
            if (tool == "shears" || tool == "minecraft:shears") {
                if (b == "web" || b.ends_with("leaves")) {
                    correct = true;
                    return 15.0;
                }
                if (b.ends_with("wool")) {
                    correct = true;
                    return 5.0;
                }
                return 1.0;
            }
            if (tool.ends_with("_sword")) {
                if (b == "web") {
                    correct = true;
                    return 15.0;
                }
                if (b == "bamboo")
                    return 50.0;
                return 1.5;
            }
            const std::string wants = LootTables::EffectiveTool(block);
            if (wants.empty() || !tool.ends_with("_" + wants))
                return 1.0;
            correct = true;
            const std::string t = Bare(tool);
            if (t.starts_with("wooden_"))
                return 2.0;
            if (t.starts_with("stone_"))
                return 4.0;
            if (t.starts_with("copper_"))
                return 5.0;
            if (t.starts_with("iron_"))
                return 6.0;
            if (t.starts_with("diamond_"))
                return 8.0;
            if (t.starts_with("netherite_"))
                return 9.0;
            if (t.starts_with("golden_"))
                return 12.0;
            return 1.0;
        }
    }

    double Session::BreakProgressPerTick(const std::string& block) const {
        const float hardness = LootTables::Hardness(block);
        if (hardness < 0)
            return 0.0;
        if (hardness == 0)
            return 1.0;
        const ItemStack& held = inventory.Held();
        const std::string tool = held.Empty() ? std::string() : ItemNameOf(held.networkId);
        bool correct = false;
        double speed = ToolSpeed(tool, block, correct);
        if (correct && speed > 1.0) {
            const int efficiency = held.Enchant(kEfficiency);
            if (efficiency > 0)
                speed += efficiency * efficiency + 1;
        }
        const int haste = std::max(EffectLevel(kHaste), EffectLevel(kConduitPower));
        if (haste > 0)
            speed *= 1.0 + 0.2 * haste;
        const int fatigue = EffectLevel(kMiningFatigue);
        if (fatigue > 0)
            speed *= std::pow(0.3, std::min(fatigue, 4));
        const bool helmetAqua = !inventory.armor[0].Empty() && inventory.armor[0].Enchant(kAquaAffinity) > 0;
        if (BlockNameAt(posX, posY, posZ).contains("water") && !helmetAqua)
            speed /= 5.0;
        if (!onGround && !flying)
            speed /= 5.0;
        const bool harvestable = LootTables::CanHarvest(block, tool);
        const double seconds = hardness * (harvestable ? 1.5 : 5.0) / speed;
        if (seconds <= 0.0)
            return 1.0;
        return 1.0 / (seconds * 20.0);
    }

    void Session::BroadcastBreakEvent(int event, int x, int y, int z, int data) const {
        for (Session* s : server.PlayersIn(dimension))
            if (s->HasSent({x >> 4, z >> 4}))
                s->SendLevelEvent(event, x + 0.5, y + 0.5, z + 0.5, data);
    }

    void Session::StartBreaking(int x, int y, int z, int) {
        if (breaking.active && breaking.x == x && breaking.y == y && breaking.z == z)
            return;
        StopBreaking();
        if (!InReach(x, y, z))
            return;
        const std::string block = BlockNameAt(x + 0.5, y + 0.5, z + 0.5);
        if (block.empty() || block == "minecraft:air")
            return;
        const double speed = BreakProgressPerTick(block);
        if (speed <= 0.0)
            return;
        breaking = {true, x, y, z, speed, 0.0};
        BroadcastBreakEvent(static_cast<int>(proto::LevelEventPacketEvent::BlockStartBreak), x, y, z,
                            static_cast<int>(std::clamp(65535.0 * speed, 0.0, 65535.0)));
    }

    void Session::StopBreaking() {
        if (!breaking.active)
            return;
        breaking.active = false;
        BroadcastBreakEvent(static_cast<int>(proto::LevelEventPacketEvent::BlockStopBreak), breaking.x, breaking.y,
                            breaking.z, 0);
    }

    void Session::BreakingTick() {
        if (!breaking.active)
            return;
        const std::string block = BlockNameAt(breaking.x + 0.5, breaking.y + 0.5, breaking.z + 0.5);
        if (block.empty() || block == "minecraft:air" || !InReach(breaking.x, breaking.y, breaking.z)) {
            StopBreaking();
            return;
        }
        const double speed = BreakProgressPerTick(block);
        if (speed <= 0.0) {
            StopBreaking();
            return;
        }
        if (std::abs(speed - breaking.speed) > 1e-4) {
            breaking.speed = speed;
            BroadcastBreakEvent(static_cast<int>(proto::LevelEventPacketEvent::BlockBreakSpeed), breaking.x,
                                breaking.y, breaking.z, static_cast<int>(std::clamp(65535.0 * speed, 0.0, 65535.0)));
        }
        breaking.progress += breaking.speed;
    }

    void Session::HandleBlockActions(const proto::PlayerAuthInputPacket& in) {
        if (isVisitor || gameMode == 6 || dead) {
            StopBreaking();
            return;
        }
        if (in.blockAction)
            for (const auto& action : *in.blockAction) {
                const auto& at = action.position;
                switch (action.action) {
                case proto::Action::StartBreak:
                case proto::Action::CrackBreak:
                    lastActive = std::chrono::steady_clock::now();
                    if (creative) {
                        StopBreaking();
                        server.PlayerBreak(*this, at.x, at.y, at.z);
                    } else {
                        StartBreaking(at.x, at.y, at.z, action.face);
                    }
                    break;
                case proto::Action::ContinueBreak:
                    lastActive = std::chrono::steady_clock::now();
                    if (!creative && (!breaking.active || breaking.x != at.x || breaking.y != at.y ||
                                      breaking.z != at.z))
                        StartBreaking(at.x, at.y, at.z, action.face);
                    break;
                case proto::Action::AbortBreak:
                    StopBreaking();
                    break;
                case proto::Action::StopBreak:
                    break;
                case proto::Action::PredictBreak:
                case proto::Action::CreativePlayerDestroyBlock:
                    lastActive = std::chrono::steady_clock::now();
                    if (breaking.active && breaking.x == at.x && breaking.y == at.y && breaking.z == at.z)
                        StopBreaking();
                    server.PlayerBreak(*this, at.x, at.y, at.z);
                    break;
                default:
                    break;
                }
            }
        BreakingTick();
    }
}
