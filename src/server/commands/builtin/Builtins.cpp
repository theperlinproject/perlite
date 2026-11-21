#include "server/commands/builtin/BuiltinSupport.hpp"

namespace perlite {
    void Server::RegisterBuiltins() {
        CommandRegistrar r;
        r.say = [](Session* to, const std::string& text) {
            if (to)
                to->SendRaw(text);
            else
                LogInfo(text);
        };
        r.fail = [say = r.say](Session* to, const std::string& text) { say(to, "§c" + text); };
        r.add = [this](std::vector<const char*> names, const char* description, bool op,
                       std::vector<std::string> notations, std::function<void(CommandCtx&)> fn) {
            std::vector<OverloadSpec> specs;
            for (const std::string& n : notations) {
                OverloadSpec spec;
                std::string error;
                if (!ParseOverload(n, spec, &error)) {
                    LogError(std::string("command ") + names.front() + ": bad overload \"" + n +
                             "\": " + error);
                    continue;
                }
                specs.push_back(std::move(spec));
            }
            for (const char* name : names) {
                CommandDef d;
                d.name = name;
                d.description = description;
                d.operatorOnly = op;
                d.overloads = specs;
                d.handler = fn;
                commands[name] = std::move(d);
            }
        };
        RegisterInfoCommands(r);
        RegisterPlayerCommands(r);
        RegisterAccessCommands(r);
        RegisterWorldCommands(r);
        RegisterServerCommands(r);
        RegisterExtraCommands(r);
    }
}
