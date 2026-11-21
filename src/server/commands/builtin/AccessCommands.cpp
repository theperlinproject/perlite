#include "server/commands/builtin/BuiltinSupport.hpp"

namespace perlite {
    void Server::RegisterAccessCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        [[maybe_unused]] const auto& fail = r.fail;
        const auto& add = r.add;
        const auto allowlistCommand = [this, say](CommandCtx& c) {
            const std::string action = str::Lower(c.raw.substr(0, c.raw.find(' ')));
            const auto persist = [this] {
                if (config.loopbackOnly)
                    return;
                std::error_code ec;
                const std::string legacy = config.dataDir + "/server.json";
                if (!std::filesystem::exists(config.dataDir + "/server.properties", ec) &&
                    std::filesystem::exists(legacy, ec))
                    config.Save(legacy);
                else
                    config.SaveProperties(config.dataDir + "/server.properties");
            };
            if (action == "on" || action == "off") {
                config.whitelist = action == "on";
                persist();
                say(c.source,
                    std::string("The allowlist is now ") + (config.whitelist ? "turned on" : "turned off"));
            } else if (action == "list") {
                std::string out =
                    "There are " + std::to_string(allowList.Entries().size()) + " allowlisted player(s)";
                if (!allowList.Entries().empty())
                    out += ": ";
                for (size_t i = 0; i < allowList.Entries().size(); i++) {
                    const AccessEntry& e = allowList.Entries()[i];
                    out += (i ? ", " : "") + (e.name.empty() ? e.xuid : e.name);
                }
                say(c.source, out);
            } else if (action == "reload") {
                allowList.Load(AllowlistPath());
                say(c.source, "Reloaded the allowlist");
            } else if (action == "add") {
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool added =
                        allowList.Add(online ? online->Name() : who, online ? online->Xuid() : std::string());
                    say(c.source,
                        added ? "Added " + who + " to the allowlist" : "Player is already allowlisted");
                }
            } else if (action == "remove") {
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool removed =
                        allowList.Remove(who) || (online && allowList.Remove(online->Xuid()));
                    say(c.source,
                        removed ? "Removed " + who + " from the allowlist" : "Player is not allowlisted");
                }
            }
        };
        const std::vector<std::string> allowOverloads = {
            "on", "off", "list", "reload", "add <targets:profiles>", "remove <targets:profiles>"};
        add({"allowlist", "whitelist"}, "Manage the allowlist", true, allowOverloads, allowlistCommand);
        add({"permission"}, "List or reload permissions.json", true, {"list", "reload"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0) {
                    std::string out = "Permissions (default " + config.defaultPermission + "), " +
                                      std::to_string(permissionList.Entries().size()) + " entries:";
                    for (const AccessEntry& e : permissionList.Entries())
                        out += "\n" + (e.name.empty() ? e.xuid : e.name) + ": " + e.permission;
                    say(c.source, out);
                } else {
                    permissionList.Load(PermissionsPath());
                    for (Session* s : InGamePlayers())
                        s->SetOperator(IsOperator(s->Name(), s->Xuid()));
                    say(c.source, "Reloaded " + PermissionsPath());
                }
            });
        add({"ban"}, "Ban players from the server", true, {"<targets:profiles> [reason:text]"},
            [this, say](CommandCtx& c) {
                const std::string reason = c.Has("reason") ? c["reason"].text : "Banned by an operator";
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool added =
                        banList.Add(online ? online->Name() : who, online ? online->Xuid() : std::string());
                    if (online)
                        online->Kick("You are banned from this server: " + reason);
                    say(c.source, added ? "Banned " + who + ": " + reason
                                        : "Nothing changed. The player is already banned");
                }
            });
        add({"pardon", "unban"}, "Lift a ban", true, {"<targets:profiles>"}, [this, say](CommandCtx& c) {
            for (const std::string& who : c["targets"].names)
                say(c.source,
                    banList.Remove(who) ? "Unbanned " + who : "Nothing changed. The player isn't banned");
        });
        add({"banlist"}, "List the banned players", true, {"", "players"}, [this, say](CommandCtx& c) {
            std::string out = "There are " + std::to_string(banList.Entries().size()) + " ban(s)";
            for (const AccessEntry& e : banList.Entries())
                out += "\n" + (e.name.empty() ? e.xuid : e.name);
            say(c.source, out);
        });
    }
}
