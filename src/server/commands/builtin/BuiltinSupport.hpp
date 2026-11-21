#pragma once

#include "server/commands/Commands.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"
#include "server/Server.hpp"
#include "world/actor/mob/MobDef.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"
#include "protocol/Packets.hpp"
#include "common/Strings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace perlite {
    struct Server::CommandRegistrar {
        std::function<void(Session*, const std::string&)> say;
        std::function<void(Session*, const std::string&)> fail;
        std::function<void(std::vector<const char*>, const char*, bool, std::vector<std::string>,
                           std::function<void(CommandCtx&)>)>
            add;
    };

    namespace detail {

        inline std::string Fixed(double v) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.2f", v);
            return buf;
        }

        inline const char* ModeName(int mode) {
            switch (mode) {
            case 1:
                return "Creative Mode";
            case 2:
                return "Adventure Mode";
            case 6:
                return "Spectator Mode";
            default:
                return "Survival Mode";
            }
        }

        inline const char* DifficultyName(int d) {
            switch (d) {
            case 0:
                return "Peaceful";
            case 2:
                return "Normal";
            case 3:
                return "Hard";
            default:
                return "Easy";
            }
        }

        inline void CollectText(const json::Value& v, std::string& out) {
            switch (v.kind) {
            case json::Value::Kind::String:
                out += v.text;
                break;
            case json::Value::Kind::Number:
                out += std::to_string(static_cast<long long>(v.number));
                break;
            case json::Value::Kind::Array:
                for (const json::Value& e : v.items)
                    CollectText(e, out);
                break;
            case json::Value::Kind::Object:
                if (v["text"].IsString())
                    out += v["text"].text;
                else if (v["translate"].IsString())
                    out += v["translate"].text;
                for (const json::Value& e : v["extra"].items)
                    CollectText(e, out);
                break;
            default:
                break;
            }
        }

        inline std::string PlainText(const std::string& raw) {
            const std::string t = str::Trim(raw);
            if (t.empty() || (t[0] != '{' && t[0] != '[' && t[0] != '"'))
                return t;
            json::Value v;
            if (!json::Parse(t, v))
                return t;
            std::string out;
            CollectText(v, out);
            return out;
        }

        inline std::string BlockPosText(int x, int y, int z) {
            return std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
        }

    }
    using namespace detail;
}
