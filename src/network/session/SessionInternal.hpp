#pragma once

#include "network/session/Session.hpp"
#include "network/auth/LoginVerifier.hpp"
#include "server/commands/Commands.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"
#include "server/Server.hpp"
#include "world/crafting/Recipes.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/actor/ActorManager.hpp"
#include "world/actor/mob/MobRegistry.hpp"
#include "protocol/Packets.hpp"
#include "common/Strings.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace perlite {
    namespace proto = proto2225;

    namespace detail {
        constexpr int32_t kProtocol = 2225;
        constexpr const char* kGameVersion = "1.26.60";
        constexpr double kEyeHeight = 1.62;
        constexpr int kChunksPerTick = 6;
        constexpr auto kIdleTimeout = std::chrono::seconds(60);
        constexpr auto kHeartbeat = std::chrono::seconds(5);
        constexpr auto kLoginTimeout = std::chrono::seconds(60);

        template <class P> bool Decode(std::span<const uint8_t> body, P& packet) {
            net::ReadStream stream(body.data(), body.size());
            return Read(stream, packet) && stream.ok;
        }

        inline std::array<uint8_t, 16> ParseUuid(const std::string& text, uint64_t fallbackSeed) {
            std::array<uint8_t, 16> text16{};
            int n = 0;
            for (size_t i = 0; i < text.size() && n < 32; i++) {
                const char c = text[i];
                int v;
                if (c >= '0' && c <= '9')
                    v = c - '0';
                else if (c >= 'a' && c <= 'f')
                    v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    v = c - 'A' + 10;
                else
                    continue;
                text16[static_cast<size_t>(n / 2)] =
                    static_cast<uint8_t>((text16[static_cast<size_t>(n / 2)] << 4) | v);
                n++;
            }
            if (n != 32) {
                for (size_t i = 0; i < 16; i++)
                    text16[i] = static_cast<uint8_t>((fallbackSeed * 0x9E3779B97F4A7C15ull) >> (i * 4));
            }
            std::array<uint8_t, 16> wire{};
            for (size_t i = 0; i < 8; i++) {
                wire[i] = text16[7 - i];
                wire[8 + i] = text16[15 - i];
            }
            return wire;
        }

        inline int ImageSide(const json::Value& v, const char* key, int fallback) {
            const int64_t n = v[key].Int(fallback);
            return n > 0 && n <= 1024 ? static_cast<int>(n) : fallback;
        }

        inline proto::Skin SkinFromClientData(const json::Value& c, const std::string& playerName) {
            proto::Skin s;
            s.skinId = c["SkinId"].String("Standard_Custom");
            s.skinResourcePack = json::Base64UrlDecode(c["SkinResourcePatch"].String());
            s.skinData.width = ImageSide(c, "SkinImageWidth", 64);
            s.skinData.height = ImageSide(c, "SkinImageHeight", 64);
            const std::string rgba = json::Base64UrlDecode(c["SkinData"].String());
            const size_t want =
                static_cast<size_t>(s.skinData.width) * static_cast<size_t>(s.skinData.height) * 4;
            s.skinData.data.assign(rgba.begin(), rgba.end());
            if (s.skinData.data.size() != want) {
                s.skinData.width = 64;
                s.skinData.height = 64;
                s.skinData.data.assign(64 * 64 * 4, 0x80);
            }
            s.capeData.width = ImageSide(c, "CapeImageWidth", 0);
            s.capeData.height = ImageSide(c, "CapeImageHeight", 0);
            const std::string cape = json::Base64UrlDecode(c["CapeData"].String());
            if (cape.size() ==
                static_cast<size_t>(s.capeData.width) * static_cast<size_t>(s.capeData.height) * 4)
                s.capeData.data.assign(cape.begin(), cape.end());
            else
                s.capeData = {};
            s.geometryData = json::Base64UrlDecode(c["SkinGeometryData"].String());
            s.geometryDataVersion = c["SkinGeometryDataEngineVersion"].String();
            s.animationData = json::Base64UrlDecode(c["SkinAnimationData"].String());
            s.capeId = c["CapeId"].String();
            s.fullSkinId = s.skinId + s.capeId;
            s.armSize = c["ArmSize"].String() == "slim" ? proto::SkinArmSize::Slim : proto::SkinArmSize::Wide;
            s.premium = c["PremiumSkin"].Bool();
            s.persona = c["PersonaSkin"].Bool();
            s.capeOnClassic = c["CapeOnClassicSkin"].Bool();
            if (const std::string color = c["SkinColor"].String(); color.size() > 1 && color[0] == '#') {
                if (uint64_t parsed; perlite::str::Parse(std::string_view(color).substr(1), parsed, 16)) {
                    const uint32_t v = static_cast<uint32_t>(parsed);
                    s.skinColor = static_cast<int32_t>(color.size() <= 7 ? (0xFF000000u | v) : v);
                }
            }
            for (const json::Value& a : c["AnimatedImageData"].items) {
                proto::SkinAnimationsEntry anim;
                anim.skinImage.width = ImageSide(a, "ImageWidth", 0);
                anim.skinImage.height = ImageSide(a, "ImageHeight", 0);
                const std::string frames = json::Base64UrlDecode(a["Image"].String());
                if (frames.size() != static_cast<size_t>(anim.skinImage.width) *
                                         static_cast<size_t>(anim.skinImage.height) * 4)
                    continue;
                anim.skinImage.data.assign(frames.begin(), frames.end());
                anim.animationType = static_cast<uint32_t>(a["Type"].Int());
                anim.animationFrames = static_cast<float>(a["Frames"].Number());
                anim.expressionType = static_cast<uint32_t>(a["AnimationExpression"].Int());
                s.animations.push_back(std::move(anim));
            }
            static const char* const kPieceNames[] = {"",
                                                      "persona_skeleton",
                                                      "persona_body",
                                                      "persona_skin",
                                                      "persona_bottom",
                                                      "persona_feet",
                                                      "persona_dress",
                                                      "persona_top",
                                                      "persona_high_pants",
                                                      "persona_hands",
                                                      "persona_outerwear",
                                                      "persona_facial_hair",
                                                      "persona_mouth",
                                                      "persona_eyes",
                                                      "persona_hair",
                                                      "persona_hood"};
            for (const json::Value& piece : c["PersonaPieces"].items) {
                proto::SkinPersonalPiecesEntry e;
                e.pieceId = piece["PieceId"].String();
                const std::string type = piece["PieceType"].String();
                for (size_t i = 1; i < std::size(kPieceNames); i++)
                    if (type == kPieceNames[i])
                        e.pieceType = static_cast<proto::PersonaPieceType>(i);
                e.packId.bytes = ParseUuid(piece["PackId"].String(), 0);
                e.isDefaultPiece = piece["IsDefault"].Bool();
                e.productId = piece["ProductId"].String();
                s.personalPieces.push_back(std::move(e));
            }
            for (const json::Value& tint : c["PieceTintColors"].items) {
                proto::SkinPieceTintColorsEntry e;
                e.pieceType = tint["PieceType"].String();
                for (size_t i = 0; i < 4 && i < tint["Colors"].items.size(); i++) {
                    const std::string col = tint["Colors"].items[i].String();
                    if (col.size() > 1 && col[0] == '#')
                        if (uint64_t parsed; perlite::str::Parse(std::string_view(col).substr(1), parsed, 16))
                            e.colors[i] = static_cast<int32_t>(static_cast<uint32_t>(parsed));
                }
                s.pieceTintColors.push_back(std::move(e));
            }
            s.trusted = "true";
            (void)playerName;
            return s;
        }

        struct FoodInfo {
            int nutrition;
            float saturation;
            bool always;
            int ticks;
            const char* leftover;
        };

        inline const FoodInfo* FoodOf(const std::string& fullName) {
            static const std::unordered_map<std::string, FoodInfo> foods = {
                {"apple", {4, 2.4f, false, 32, nullptr}},
                {"baked_potato", {5, 6.0f, false, 32, nullptr}},
                {"beef", {3, 1.8f, false, 32, nullptr}},
                {"beetroot", {1, 1.2f, false, 32, nullptr}},
                {"beetroot_soup", {6, 7.2f, false, 32, "minecraft:bowl"}},
                {"bread", {5, 6.0f, false, 32, nullptr}},
                {"carrot", {3, 3.6f, false, 32, nullptr}},
                {"chicken", {2, 1.2f, false, 32, nullptr}},
                {"chorus_fruit", {4, 2.4f, true, 32, nullptr}},
                {"cod", {2, 0.4f, false, 32, nullptr}},
                {"cooked_beef", {8, 12.8f, false, 32, nullptr}},
                {"cooked_chicken", {6, 7.2f, false, 32, nullptr}},
                {"cooked_cod", {5, 6.0f, false, 32, nullptr}},
                {"cooked_mutton", {6, 9.6f, false, 32, nullptr}},
                {"cooked_porkchop", {8, 12.8f, false, 32, nullptr}},
                {"cooked_rabbit", {5, 6.0f, false, 32, nullptr}},
                {"cooked_salmon", {6, 9.6f, false, 32, nullptr}},
                {"cookie", {2, 0.4f, false, 32, nullptr}},
                {"dried_kelp", {1, 0.6f, false, 16, nullptr}},
                {"enchanted_golden_apple", {4, 9.6f, true, 32, nullptr}},
                {"golden_apple", {4, 9.6f, true, 32, nullptr}},
                {"glow_berries", {2, 0.4f, false, 32, nullptr}},
                {"golden_carrot", {6, 14.4f, false, 32, nullptr}},
                {"honey_bottle", {6, 1.2f, false, 40, "minecraft:glass_bottle"}},
                {"melon_slice", {2, 1.2f, false, 32, nullptr}},
                {"mushroom_stew", {6, 7.2f, false, 32, "minecraft:bowl"}},
                {"mutton", {2, 1.2f, false, 32, nullptr}},
                {"poisonous_potato", {2, 1.2f, false, 32, nullptr}},
                {"porkchop", {3, 1.8f, false, 32, nullptr}},
                {"potato", {1, 0.6f, false, 32, nullptr}},
                {"pufferfish", {1, 0.2f, false, 32, nullptr}},
                {"pumpkin_pie", {8, 4.8f, false, 32, nullptr}},
                {"rabbit", {3, 1.8f, false, 32, nullptr}},
                {"rabbit_stew", {10, 12.0f, false, 32, "minecraft:bowl"}},
                {"rotten_flesh", {4, 0.8f, false, 32, nullptr}},
                {"salmon", {2, 0.4f, false, 32, nullptr}},
                {"spider_eye", {2, 3.2f, false, 32, nullptr}},
                {"suspicious_stew", {6, 7.2f, true, 32, "minecraft:bowl"}},
                {"sweet_berries", {2, 0.4f, false, 32, nullptr}},
                {"tropical_fish", {1, 0.2f, false, 32, nullptr}},
                {"milk_bucket", {0, 0.0f, true, 32, "minecraft:bucket"}},
                {"potion", {0, 0.0f, true, 32, "minecraft:glass_bottle"}},
            };
            std::string n = fullName.starts_with("minecraft:") ? fullName.substr(10) : fullName;
            if (n == "clownfish")
                n = "tropical_fish";
            if (n == "appleEnchanted")
                n = "enchanted_golden_apple";
            const auto it = foods.find(n);
            return it == foods.end() ? nullptr : &it->second;
        }

        inline std::string ItemNameOf(int16_t id) {
            const CatalogItem* item = BlockCatalog::Instance().ItemById(id);
            return item ? item->name : std::string();
        }

        inline std::string Pretty(const std::string& id) {
            std::string n = id.starts_with("minecraft:") ? id.substr(10) : id;
            if (n == "zombie_pigman")
                n = "zombified_piglin";
            if (n == "evocation_illager")
                n = "evoker";
            if (n.ends_with("_v2"))
                n.resize(n.size() - 3);
            bool up = true;
            for (char& c : n) {
                if (c == '_') {
                    c = ' ';
                    up = true;
                } else if (up) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    up = false;
                }
            }
            return n;
        }

        inline int XpForLevel(int level) {
            if (level >= 30)
                return 112 + (level - 30) * 9;
            if (level >= 15)
                return 37 + (level - 15) * 5;
            return 7 + level * 2;
        }

        inline bool BypassesArmor(Session::DamageCause c) {
            using C = Session::DamageCause;
            return c == C::Fall || c == C::Void || c == C::Drowning || c == C::Starving || c == C::Suffocation ||
                   c == C::Poison || c == C::Wither || c == C::Magic || c == C::Kill || c == C::Burning;
        }

        inline bool FireCause(Session::DamageCause c) {
            using C = Session::DamageCause;
            return c == C::Lava || c == C::Fire || c == C::Burning || c == C::Magma;
        }

        inline uint8_t MobAngle(float degrees) {
            return static_cast<uint8_t>(static_cast<int>(std::floor(degrees / 360.0f * 256.0f)) & 255);
        }

        inline proto::MetadataDictionaryEntry FloatEntry(proto::MetadataDictionaryEntryKey key, float value) {
            proto::MetadataDictionaryEntry e;
            e.key = key;
            e.type = proto::MetadataDictionaryEntryType::Float;
            e.legacyType = static_cast<uint8_t>(e.type); // DataItem Type byte, = index
            auto& inner = e.value.template emplace<4>();
            inner.template emplace<4>(value);
            return e;
        }

        inline proto::MetadataDictionaryEntry IntEntry(proto::MetadataDictionaryEntryKey key, int32_t value) {
            proto::MetadataDictionaryEntry e;
            e.key = key;
            e.type = proto::MetadataDictionaryEntryType::Int;
            e.legacyType = static_cast<uint8_t>(e.type); // DataItem Type byte, = index
            auto& inner = e.value.template emplace<4>();
            inner.template emplace<3>(value);
            return e;
        }

        inline std::vector<proto::MetadataDictionaryEntry> MobMetadata(const Mob& mob) {
            std::vector<proto::MetadataDictionaryEntry> out;
            if (mob.kind == actorkind::FallingBlock)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::Variant, BlockRegistry::Instance().Hash(mob.block)));
            if (mob.kind == actorkind::PrimedTnt)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::FuseLength, std::max(0, mob.fuse)));
            if (mob.kind == actorkind::ExperienceOrb)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::ExperienceValue, mob.block));
            proto::MetadataDictionaryEntry flags;
            flags.key = proto::MetadataDictionaryEntryKey::Flags;
            flags.type = proto::MetadataDictionaryEntryType::Long;
            flags.legacyType = static_cast<uint8_t>(flags.type); // DataItem Type byte, = index
            flags.value.template emplace<1>(static_cast<int64_t>(ActorManager::Flags(mob)));
            out.push_back(flags);
            const MobBody body = MobRegistry::Instance().BodyOf(mob);
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::Scale, body.scale));
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::BoundingboxWidth, body.width));
            out.push_back(FloatEntry(proto::MetadataDictionaryEntryKey::BoundingboxHeight, body.height));
            if (mob.variant >= 0)
                out.push_back(IntEntry(proto::MetadataDictionaryEntryKey::Variant, mob.variant));
            return out;
        }

    }
    using namespace detail;

    template <class P> void Session::Send(const P& packet) {
        std::vector<uint8_t> body;
        if (!net::EncodeChecked(packet, body)) {
            LogWarn(std::string("refusing to send malformed ") + P::kName);
            MalformedSends()++;
            return;
        }
        Queue(P::kId, body);
    }
}
