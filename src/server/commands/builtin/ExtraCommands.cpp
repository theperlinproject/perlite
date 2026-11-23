#include "server/commands/builtin/BuiltinSupport.hpp"
#include "server/access/BanList.hpp"
#include "world/level/block/BlockCatalog.hpp"

namespace perlite {
    void Server::RegisterExtraCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        const auto& fail = r.fail;
        const auto& add = r.add;

        add({"about", "version", "ver"}, "Show the server software and version", false, {""},
            [say](CommandCtx& c) {
                say(c.source, "This server is running Perlite 0.2.0 for Minecraft: Bedrock Edition 1.26.60 "
                              "(protocol 2225)");
            });

        add({"status", "tps"}, "Show the server's performance", true, {""}, [this, say](CommandCtx& c) {
            const double tps = TicksPerSecond();
            size_t chunksLoaded = chunks ? chunks->Loaded() : 0;
            for (const auto& w : otherWorlds)
                if (w)
                    chunksLoaded += w->Loaded();
            say(c.source, "TPS: " + Fixed(tps) + " | players: " + std::to_string(InGamePlayers().size()) +
                              "/" + std::to_string(config.maxPlayers) +
                              " | chunks: " + std::to_string(chunksLoaded) +
                              " | mobs: " + std::to_string(MobsIn(0).Count() + MobsIn(1).Count() + MobsIn(2).Count()) +
                              " | uptime: " + std::to_string(TickCount() / 20) + "s");
        });

        add({"save"}, "Control or check how the server saves the world", true,
            {"", "<action:enum(hold|resume|query|now)>"}, [this, say, fail](CommandCtx& c) {
                const std::string action = c.Has("action") ? c["action"].text : std::string("now");
                if (!HasStorage()) {
                    fail(c.source, "World saving is off (perlite-world-storage=none)");
                    return;
                }
                if (action == "hold") {
                    savingHeld = true;
                    SaveWorld(true);
                    say(c.source, "Saving... the world is ready to be copied; run /save resume when done");
                } else if (action == "resume") {
                    savingHeld = false;
                    say(c.source, "Changes to the world are resumed.");
                } else if (action == "query") {
                    say(c.source, savingHeld ? "Data saved. Files are now ready to be copied."
                                             : "A previous save has not been held; run /save hold first");
                } else {
                    SaveWorld(true);
                    say(c.source, "Saved the world");
                }
            });
        add({"save-all"}, "Save the world now", true, {""}, [this, say, fail](CommandCtx& c) {
            if (!HasStorage()) {
                fail(c.source, "World saving is off (perlite-world-storage=none)");
                return;
            }
            SaveWorld(true);
            say(c.source, "Saved the world");
        });

        add({"setmaxplayers"}, "Set the most players that can join", true, {"<maxPlayers:int>"},
            [this, say](CommandCtx& c) {
                const int n = static_cast<int>(std::clamp<int64_t>(c["maxPlayers"].integer, 1, 1000));
                config.maxPlayers = n;
                say(c.source, "Set max players to " + std::to_string(n));
            });

        add({"daylock", "alwaysday"}, "Lock the time of day", true, {"", "<lock:enum(true|false)>"},
            [this, say](CommandCtx& c) {
                const bool lock = !c.Has("lock") || c["lock"].text == "true";
                SetRule("dodaylightcycle", lock ? 0 : 1);
                if (lock)
                    SetDayTime(5000);
                say(c.source, lock ? "Day-Night cycle locked" : "Day-Night cycle unlocked");
            });

        add({"ban-ip"}, "Ban an address or the address of a player", true, {"<target:string> [reason:text]"},
            [this, say, fail](CommandCtx& c) {
                std::string target = c["target"].text;
                std::string ip = target;
                if (!IpBanList::LooksLikeIp(target)) {
                    Session* s = FindByName(target);
                    if (!s || s->RemoteAddress().empty()) {
                        fail(c.source, "Invalid IP address or unknown player");
                        return;
                    }
                    ip = s->RemoteAddress();
                }
                const std::string reason = c.Has("reason") ? c["reason"].text : std::string();
                if (!ipBans.Add(ip, reason, c.source ? c.source->Name() : "Server")) {
                    fail(c.source, "Nothing changed. That IP is already banned");
                    return;
                }
                for (Session* s : InGamePlayers())
                    if (s->RemoteAddress() == ip)
                        s->Kick(reason.empty() ? "You have been IP banned." : reason);
                say(c.source, "Banned IP " + ip + (reason.empty() ? std::string() : ": " + reason));
            });
        add({"pardon-ip", "unban-ip"}, "Remove an address from the ban list", true, {"<ip:string>"},
            [this, say, fail](CommandCtx& c) {
                if (!ipBans.Remove(c["ip"].text)) {
                    fail(c.source, "Nothing changed. That IP isn't banned");
                    return;
                }
                say(c.source, "Unbanned IP " + c["ip"].text);
            });
        add({"banlist-ips"}, "List banned addresses", true, {""}, [this, say](CommandCtx& c) {
            const auto entries = ipBans.Entries();
            std::string list;
            for (const IpBan& b : entries)
                list += (list.empty() ? "" : ", ") + b.ip;
            say(c.source, "There are " + std::to_string(entries.size()) + " banned IPs" +
                              (list.empty() ? std::string() : ": " + list));
        });

        add({"transfer"}, "Send players to another server", true,
            {"<targets:targets> <address:string> [port:int]"}, [say](CommandCtx& c) {
                const uint16_t port =
                    c.Has("port") ? static_cast<uint16_t>(std::clamp<int64_t>(c["port"].integer, 1, 65535))
                                  : 19132;
                for (Session* s : c["targets"].players)
                    s->Transfer(c["address"].text, port);
                say(c.source, "Transferred " + std::to_string(c["targets"].players.size()) +
                                  " player(s) to " + c["address"].text + ":" + std::to_string(port));
            });

        add({"playsound"}, "Play a sound", true,
            {"<sound:string> [targets:targets] [position:pos] [volume:float] [pitch:float]"},
            [say, fail](CommandCtx& c) {
                std::vector<Session*> targets =
                    c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (targets.empty() && c.source)
                    targets.push_back(c.source);
                if (targets.empty()) {
                    fail(c.source, "No player was found");
                    return;
                }
                const float volume = c.Has("volume") ? static_cast<float>(c["volume"].number) : 1.0f;
                const float pitch = c.Has("pitch") ? static_cast<float>(c["pitch"].number) : 1.0f;
                for (Session* s : targets) {
                    const double x = c.Has("position") ? c["position"].pos[0] : s->X();
                    const double y = c.Has("position") ? c["position"].pos[1] : s->Y();
                    const double z = c.Has("position") ? c["position"].pos[2] : s->Z();
                    s->PlaySoundTo(c["sound"].text, x, y, z, volume, pitch);
                }
                say(c.source, "Played sound " + c["sound"].text);
            });
        add({"stopsound"}, "Stop a sound", true, {"<targets:targets> [sound:string]"}, [say](CommandCtx& c) {
            for (Session* s : c["targets"].players)
                s->StopSound(c.Has("sound") ? c["sound"].text : std::string());
            say(c.source, "Stopped sounds");
        });
        add({"particle"}, "Create a particle", true, {"<effect:string> [position:pos]"},
            [this](CommandCtx& c) {
                const int dim = CommandDimension(c.source);
                double x = SpawnX(), y = SpawnY(), z = SpawnZ();
                if (c.source) {
                    x = c.source->X();
                    y = c.source->Y();
                    z = c.source->Z();
                }
                if (c.Has("position")) {
                    x = c["position"].pos[0];
                    y = c["position"].pos[1];
                    z = c["position"].pos[2];
                }
                for (Session* s : PlayersIn(dim))
                    s->SpawnParticle(c["effect"].text, x, y, z);
            });

        add({"damage"}, "Damage players", true, {"<targets:targets> <amount:int>"}, [say](CommandCtx& c) {
            const float amount = static_cast<float>(std::max<int64_t>(0, c["amount"].integer));
            int hit = 0;
            for (Session* s : c["targets"].players)
                if (s->Damage(amount, Session::DamageCause::Generic, s->X(), s->Z()))
                    hit++;
            say(c.source, "Applied damage to " + std::to_string(hit) + " player(s)");
        });

        add({"tag"}, "Manage the tags on players", true,
            {"<targets:targets> add <name:string>", "<targets:targets> remove <name:string>",
             "<targets:targets> list"},
            [say](CommandCtx& c) {
                const auto& players = c["targets"].players;
                if (c.overload == 0) {
                    int added = 0;
                    for (Session* s : players)
                        added += s->Tags().insert(c["name"].text).second ? 1 : 0;
                    say(c.source,
                        "Added tag '" + c["name"].text + "' to " + std::to_string(added) + " entities");
                } else if (c.overload == 1) {
                    int removed = 0;
                    for (Session* s : players)
                        removed += static_cast<int>(s->Tags().erase(c["name"].text));
                    say(c.source,
                        "Removed tag '" + c["name"].text + "' from " + std::to_string(removed) + " entities");
                } else {
                    for (Session* s : players) {
                        std::string list;
                        for (const std::string& t : s->Tags())
                            list += (list.empty() ? "" : ", ") + t;
                        say(c.source, s->Name() + " has " + std::to_string(s->Tags().size()) + " tags" +
                                          (list.empty() ? std::string() : ": " + list));
                    }
                }
            });

        add({"testfor"}, "Count the players that match a selector", true, {"<victim:targets>"},
            [say, fail](CommandCtx& c) {
                const auto& players = c["victim"].players;
                if (players.empty()) {
                    fail(c.source, "No targets matched selector");
                    return;
                }
                std::string names;
                for (Session* s : players)
                    names += (names.empty() ? "" : ", ") + s->Name();
                say(c.source, "Found " + names);
            });

        add({"testforblock"}, "Test whether a block is at a position", true,
            {"<position:blockpos> <block:block>"}, [this, say, fail](CommandCtx& c) {
                const int x = static_cast<int>(c["position"].pos[0]),
                          y = static_cast<int>(c["position"].pos[1]),
                          z = static_cast<int>(c["position"].pos[2]);
                const int dim = CommandDimension(c.source);
                ChunkManager& world = World(dim);
                if (!world.Peek({x >> 4, z >> 4})) {
                    fail(c.source, "Cannot test for block outside of the world");
                    return;
                }
                const std::string here = BlockRegistry::Instance().Name(world.GetBlock(x, y, z));
                std::string want = c["block"].text;
                want = want.substr(0, want.find('['));
                if (!want.contains(':'))
                    want = "minecraft:" + want;
                if (here == want)
                    say(c.source, "Successfully found the block at " + BlockPosText(x, y, z) + ".");
                else
                    fail(c.source, "The block at " + BlockPosText(x, y, z) + " is " + here +
                                       " (expected: " + want + ").");
            });
    }
}
