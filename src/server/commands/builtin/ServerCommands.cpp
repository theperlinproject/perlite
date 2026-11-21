#include "server/commands/builtin/BuiltinSupport.hpp"

namespace perlite {
    void Server::RegisterServerCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        [[maybe_unused]] const auto& fail = r.fail;
        const auto& add = r.add;
        add({"stop"}, "Stop the server", true, {""}, [this, say](CommandCtx& c) {
            say(c.source, "Stopping the server");
            RequestStop();
        });
        add({"save-all"}, "Save the world", true, {"", "flush"}, [say](CommandCtx& c) {
            say(c.source, "Saved the game (this server keeps no world file yet, so nothing was written)");
        });
        add({"save-on"}, "Turn automatic saving on", true, {""},
            [say](CommandCtx& c) { say(c.source, "Automatic saving is already turned on"); });
        add({"save-off"}, "Turn automatic saving off", true, {""},
            [say](CommandCtx& c) { say(c.source, "Automatic saving is already turned off"); });
    }
}
