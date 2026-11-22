#include "world/actor/mob/VanillaEntities.hpp"

#include "common/FileIo.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"
#include "common/Strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace perlite {
    namespace fs = std::filesystem;

    namespace {
        // The pack's files carry // and /* */ comments, which strict JSON does not allow.
        std::string StripComments(const std::string& in) {
            std::string out;
            out.reserve(in.size());
            bool inString = false;
            for (size_t i = 0; i < in.size(); i++) {
                const char c = in[i];
                if (inString) {
                    out += c;
                    if (c == '\\' && i + 1 < in.size())
                        out += in[++i];
                    else if (c == '"')
                        inString = false;
                    continue;
                }
                if (c == '"') {
                    inString = true;
                    out += c;
                } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
                    while (i < in.size() && in[i] != '\n')
                        i++;
                    out += '\n';
                } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/'))
                        i++;
                    i++;
                } else {
                    out += c;
                }
            }
            return out;
        }

        // A number, a [min, max] pair or a {range_min, range_max} object; ranges give their middle.
        bool Amount(const json::Value& v, float& out) {
            if (v.kind == json::Value::Kind::Number) {
                out = static_cast<float>(v.Number());
                return true;
            }
            if (v.IsArray() && v.Size() >= 2) {
                out = static_cast<float>((v[0].Number() + v[1].Number()) / 2);
                return true;
            }
            if (v.IsObject()) {
                if (v["range_min"].kind == json::Value::Kind::Number && v["range_max"].kind == json::Value::Kind::Number) {
                    out = static_cast<float>((v["range_min"].Number() + v["range_max"].Number()) / 2);
                    return true;
                }
                if (v["min"].kind == json::Value::Kind::Number && v["max"].kind == json::Value::Kind::Number) {
                    out = static_cast<float>((v["min"].Number() + v["max"].Number()) / 2);
                    return true;
                }
            }
            return false;
        }

        struct Parts {
            bool hasBox = false, hasScale = false, hasHealth = false, hasSpeed = false, hasDamage = false,
                 hasVariant = false;
            float width = 0, height = 0, scale = 1, health = 0, speed = 0, damage = 0;
            int variant = 0;
        };

        Parts Read(const json::Value& components) {
            Parts p;
            if (!components.IsObject())
                return p;
            if (const json::Value& box = components["minecraft:collision_box"]; box.IsObject()) {
                p.hasBox = true;
                p.width = static_cast<float>(box["width"].Number(0.6));
                p.height = static_cast<float>(box["height"].Number(1.8));
            }
            if (const json::Value& scale = components["minecraft:scale"]; scale.IsObject())
                p.hasScale = Amount(scale["value"], p.scale);
            if (const json::Value& health = components["minecraft:health"]; health.IsObject()) {
                if (health["max"].kind == json::Value::Kind::Number)
                    p.hasHealth = Amount(health["max"], p.health);
                else
                    p.hasHealth = Amount(health["value"], p.health);
            }
            if (const json::Value& move = components["minecraft:movement"]; move.IsObject())
                p.hasSpeed = Amount(move["value"], p.speed);
            if (const json::Value& attack = components["minecraft:attack"]; attack.IsObject())
                p.hasDamage = Amount(attack["damage"], p.damage);
            if (const json::Value& variant = components["minecraft:variant"]; variant.IsObject()) {
                p.hasVariant = variant["value"].kind == json::Value::Kind::Number;
                p.variant = static_cast<int>(variant["value"].Int());
            }
            return p;
        }

        bool Contains(const std::string& haystack, const char* needle) {
            return haystack.find(needle) != std::string::npos;
        }
    }

    VanillaEntities& VanillaEntities::Instance() {
        static VanillaEntities instance;
        return instance;
    }

    bool VanillaEntities::ParseEntity(const std::string& text, VanillaEntity& out) {
        json::Value root;
        if (!json::Parse(StripComments(text), root))
            return false;
        const json::Value& entity = root["minecraft:entity"];
        if (!entity.IsObject())
            return false;
        out = VanillaEntity{};
        out.identifier = entity["description"]["identifier"].String();
        if (out.identifier.empty())
            return false;

        const Parts base = Read(entity["components"]);
        Parts adult = base, baby;
        bool haveBaby = false;
        const json::Value& groups = entity["component_groups"];
        if (groups.IsObject()) {
            for (const auto& [rawName, group] : groups.members) {
                const std::string name = str::Lower(rawName);
                const Parts g = Read(group);
                // Size classes: a group that sets the variant the client sizes the model from, plus
                // its own collision box (slime, magma cube).
                if (g.hasVariant && g.hasBox &&
                    (Contains(name, "large") || Contains(name, "medium") || Contains(name, "small") ||
                     Contains(name, "size"))) {
                    VanillaSize size;
                    size.variant = g.variant;
                    size.width = g.width;
                    size.height = g.height;
                    size.health = g.hasHealth ? g.health : 0;
                    size.damage = g.hasDamage ? g.damage : -1;
                    out.sizes.push_back(size);
                    continue;
                }
                // Coats picked at spawn (rabbits). Desert and salt-and-pepper coats belong to their
                // biomes, so they are not picked at random.
                if (g.hasVariant && Contains(name, "coat") && !Contains(name, "desert") && !Contains(name, "salt"))
                    out.variants.push_back(g.variant);
                if (Contains(name, "baby") && !haveBaby) {
                    baby = g;
                    haveBaby = true;
                } else if (Contains(name, "adult")) {
                    if (g.hasBox) {
                        adult.hasBox = true;
                        adult.width = g.width;
                        adult.height = g.height;
                    }
                    if (g.hasScale) {
                        adult.hasScale = true;
                        adult.scale = g.scale;
                    }
                    if (g.hasHealth) {
                        adult.hasHealth = true;
                        adult.health = g.health;
                    }
                    if (g.hasSpeed) {
                        adult.hasSpeed = true;
                        adult.speed = g.speed;
                    }
                    if (g.hasDamage) {
                        adult.hasDamage = true;
                        adult.damage = g.damage;
                    }
                }
            }
        }
        std::ranges::sort(out.sizes, {}, &VanillaSize::variant);
        std::ranges::sort(out.variants);
        out.variants.erase(std::unique(out.variants.begin(), out.variants.end()), out.variants.end());

        if (adult.hasBox) {
            out.width = adult.width;
            out.height = adult.height;
        }
        out.scale = adult.hasScale ? adult.scale : 1.0f;
        if (adult.hasHealth)
            out.health = adult.health;
        if (adult.hasSpeed)
            out.speed = adult.speed;
        if (adult.hasDamage)
            out.damage = adult.damage;
        if (haveBaby) {
            if (baby.hasBox) {
                out.babyWidth = baby.width;
                out.babyHeight = baby.height;
            }
            if (baby.hasScale)
                out.babyScale = baby.scale;
        }
        // Size classes replace the single box: the largest is the default.
        if (!out.sizes.empty() && out.width == 0) {
            out.width = out.sizes.back().width;
            out.height = out.sizes.back().height;
        }
        return true;
    }

    size_t VanillaEntities::Load(const std::string& behaviorPackDir) {
        entities.clear();
        std::error_code ec;
        const fs::path dir = fs::path(behaviorPackDir) / "entities";
        if (behaviorPackDir.empty() || !fs::is_directory(dir, ec))
            return 0;
        size_t failed = 0;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".json")
                continue;
            std::string text;
            VanillaEntity entity;
            if (!file::Read(e.path(), text) || !ParseEntity(text, entity)) {
                failed++;
                continue;
            }
            entities[entity.identifier] = std::move(entity);
        }
        if (failed)
            LogWarn("Vanilla behavior pack: " + std::to_string(failed) + " entity file(s) could not be read");
        return entities.size();
    }

    const VanillaEntity* VanillaEntities::Get(const std::string& identifier) const {
        const auto it = entities.find(identifier);
        return it == entities.end() ? nullptr : &it->second;
    }
}
