#include "server/commands/builtin/BuiltinSupport.hpp"

namespace perlite {
    void Server::RegisterInfoCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        [[maybe_unused]] const auto& fail = r.fail;
        const auto& add = r.add;
        add({"help", "?"}, "List the commands you can use, or how to use one", false,
            {"", "<command:string>"}, [this, say, fail](CommandCtx& c) {
                Session* from = c.source;
                const auto usable = [&](const CommandDef& d) {
                    return !d.operatorOnly || !from || from->Operator();
                };
                if (c.Has("command")) {
                    const std::string name =
                        str::Lower(c["command"].text.empty() || c["command"].text[0] != '/'
                                       ? c["command"].text
                                       : c["command"].text.substr(1));
                    const auto it = commands.find(name);
                    if (it == commands.end() || !usable(it->second)) {
                        fail(from, "Unknown command: " + name);
                        return;
                    }
                    if (it->second.overloads.empty())
                        say(from, "/" + name + " - " + it->second.description);
                    for (const OverloadSpec& o : it->second.overloads)
                        say(from, UsageLine(name, o));
                    return;
                }
                for (const auto& [name, d] : commands) {
                    if (!usable(d))
                        continue;
                    say(from, d.overloads.empty() ? "/" + name : UsageLine(name, d.overloads.front()));
                }
            });
        add({"list"}, "Show who is online", false, {"", "uuids"}, [this, say](CommandCtx& c) {
            std::string names;
            const auto players = InGamePlayers();
            for (Session* p : players)
                names += (names.empty() ? "" : ", ") + p->Name();
            say(c.source, "There are " + std::to_string(players.size()) + " of a max of " +
                              std::to_string(config.maxPlayers) + " players online: " + names);
        });
        add({"plugins", "pl"}, "List loaded plugins", false, {""}, [this, say](CommandCtx& c) {
            std::string out = "Plugins:";
            for (const auto& p : plugins->List())
                out += "\n" + p.name + (p.version.empty() ? "" : " " + p.version) +
                       (p.enabled ? "" : " (disabled: " + p.error + ")");
            say(c.source, out);
        });
        add({"seed"}, "Show the world seed", true, {""}, [this, say](CommandCtx& c) {
            say(c.source, "Seed: [" + std::to_string(static_cast<int64_t>(config.seed)) + "]");
        });

        add({"say"}, "Broadcast a message to everyone", true, {"<message:text>"}, [this](CommandCtx& c) {
            Broadcast("[" + (c.source ? c.source->Name() : std::string("Server")) + "] " + c["message"].text);
        });
        add({"me"}, "Show a message about yourself", false, {"<action:text>"}, [this](CommandCtx& c) {
            Broadcast("* " + (c.source ? c.source->Name() : std::string("Server")) + " " + c["action"].text);
        });
        add({"tell", "msg", "w"}, "Send a private message", false, {"<targets:targets> <message:text>"},
            [say](CommandCtx& c) {
                const std::string from = c.source ? c.source->Name() : "Server";
                for (Session* s : c["targets"].players) {
                    say(c.source, "§7You whisper to " + s->Name() + ": " + c["message"].text);
                    s->SendRaw("§7" + from + " whispers to you: " + c["message"].text);
                }
            });
        add({"tellraw"}, "Send a message to players", true, {"<targets:targets> <message:text>"},
            [](CommandCtx& c) {
                const std::string text = PlainText(c["message"].text);
                for (Session* s : c["targets"].players)
                    s->SendRaw(text);
            });
        add({"title"}, "Show a title, subtitle or action bar message", true,
            {"<targets:targets> <kind:enum(title|subtitle|actionbar)> <text:text>",
             "<targets:targets> <kind:enum(clear|reset)>",
             "<targets:targets> times <fadeIn:int> <stay:int> <fadeOut:int>"},
            [say](CommandCtx& c) {
                const std::string kind = c.Has("kind") ? c["kind"].text : "times";
                for (Session* s : c["targets"].players) {
                    if (kind == "clear")
                        s->SendTitle(0, "");
                    else if (kind == "reset")
                        s->SendTitle(1, "");
                    else if (kind == "title")
                        s->SendTitle(2, PlainText(c["text"].text));
                    else if (kind == "subtitle")
                        s->SendTitle(3, PlainText(c["text"].text));
                    else if (kind == "actionbar")
                        s->SendTitle(4, PlainText(c["text"].text));
                    else
                        s->SendTitle(5, "", static_cast<int>(c["fadeIn"].integer),
                                     static_cast<int>(c["stay"].integer),
                                     static_cast<int>(c["fadeOut"].integer));
                }
                const size_t n = c["targets"].players.size();
                say(c.source, kind == "times" ? "Set the title times for " + std::to_string(n) + " player(s)"
                                              : (kind == "clear" || kind == "reset"
                                                     ? std::string(kind == "clear" ? "Cleared" : "Reset") +
                                                           " the titles of "
                                                     : "Showed the " + kind + " to ") +
                                                    std::to_string(n) + " player(s)");
            });
    }
}
