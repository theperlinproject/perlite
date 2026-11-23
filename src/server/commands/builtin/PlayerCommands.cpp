#include "server/commands/builtin/BuiltinSupport.hpp"
#include "world/actor/mob/MobRegistry.hpp"

namespace perlite {
    void Server::RegisterPlayerCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        [[maybe_unused]] const auto& fail = r.fail;
        const auto& add = r.add;
        add({"teleport", "tp"}, "Teleport entities", true,
            {"<destination:target>", "<location:pos>", "<targets:targets> <destination:target>",
             "<targets:targets> <location:pos> [yaw:float] [pitch:float]"},
            [this, say, fail](CommandCtx& c) {
                std::vector<Session*> who = c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "Only a player can teleport themselves; use /tp <targets> "
                                      "<destination> from the console");
                        return;
                    }
                    who.push_back(c.source);
                }
                if (c.Has("destination")) {
                    Session* to = c["destination"].players.front();
                    for (Session* s : who) {
                        if (s == to)
                            continue;
                        if (s->DimensionId() != to->DimensionId()) {
                            ChangeDimension(*s, to->DimensionId(), to->X(), to->Y(), to->Z());
                            say(c.source, "Teleported " + s->Name() + " to " + to->Name());
                            continue;
                        }
                        s->Teleport(to->X(), to->Y(), to->Z());
                        say(c.source, "Teleported " + s->Name() + " to " + to->Name());
                    }
                    return;
                }
                const auto& p = c["location"].pos;
                const int dim = CommandDimension(c.source);
                for (Session* s : who) {
                    if (s->DimensionId() != dim) {
                        ChangeDimension(*s, dim, p[0], p[1] + 1.62, p[2]);
                        say(c.source, "Teleported " + s->Name() + " to " + Fixed(p[0]) + ", " + Fixed(p[1]) + ", " + Fixed(p[2]));
                        continue;
                    }
                    if (c.Has("yaw"))
                        s->Teleport(p[0], p[1] + 1.62, p[2], static_cast<float>(c["yaw"].number),
                                    static_cast<float>(c["pitch"].number));
                    else
                        s->Teleport(p[0], p[1] + 1.62, p[2]);
                    say(c.source, "Teleported " + s->Name() + " to " + Fixed(p[0]) + ", " + Fixed(p[1]) +
                                      ", " + Fixed(p[2]));
                }
            });

        add({"kick"}, "Kick a player off the server", true, {"<targets:targets> [reason:text]"},
            [say](CommandCtx& c) {
                const std::string reason = c.Has("reason") ? c["reason"].text : "Kicked by an operator";
                for (Session* s : c["targets"].players) {
                    const std::string name = s->Name();
                    s->Kick(reason);
                    say(c.source, "Kicked " + name + ": " + reason);
                }
            });
        add({"op"}, "Make a player a server operator", true, {"<targets:profiles>"},
            [this, say](CommandCtx& c) {
                for (const std::string& n : c["targets"].names)
                    say(c.source, SetOperator(n, true));
            });
        add({"deop"}, "Remove a player's operator status", true, {"<targets:profiles>"},
            [this, say](CommandCtx& c) {
                for (const std::string& n : c["targets"].names)
                    say(c.source, SetOperator(n, false));
            });
        add({"gamemode"}, "Set a player's game mode", true,
            {"<mode:enum(survival|creative|adventure|spectator)> [target:targets]"}, [say](CommandCtx& c) {
                const std::string m = c["mode"].text;
                const int value = m == "survival" ? 0 : m == "creative" ? 1 : m == "adventure" ? 2 : 6;
                std::vector<Session*> list = c.Has("target") ? c["target"].players : std::vector<Session*>{};
                if (!c.Has("target")) {
                    if (!c.source) {
                        say(nullptr,
                            "§cThe console has no game mode; name a player: /gamemode <mode> <target>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    s->SetGameMode(value);
                    if (s != c.source)
                        s->SendRaw(std::string("Your game mode has been updated to ") + ModeName(value));
                    say(c.source, s == c.source ? std::string("Set own game mode to ") + ModeName(value)
                                                : "Set " + s->Name() + "'s game mode to " + ModeName(value));
                }
            });
        add({"defaultgamemode"}, "Set the game mode new players start in", true,
            {"<mode:enum(survival|creative|adventure|spectator)>"}, [this, say](CommandCtx& c) {
                const std::string m = c["mode"].text;
                config.gameMode = m == "survival" ? 0 : m == "creative" ? 1 : m == "adventure" ? 2 : 6;
                say(c.source, std::string("The default game mode is now ") + ModeName(config.gameMode));
            });
        add({"flyspeed"}, "Change your spectator flying speed (1x, 2x, 5x, 10x or 20x)", false,
            {"", "<speed:string>"}, [fail](CommandCtx& c) {
                if (!c.source) {
                    fail(nullptr, "Only a player in spectator mode has a flying speed to change");
                    return;
                }
                Session* from = c.source;
                if (!from->Spectator()) {
                    from->SendActionBar("Flying speed can only be changed in spectator mode");
                    return;
                }
                if (!c.Has("speed")) {
                    from->SendActionBar("Flying speed: " + std::to_string(from->FlySpeedLevel()) + "x");
                    return;
                }
                std::string text = c["speed"].text;
                if (!text.empty() && (text.back() == 'x' || text.back() == 'X'))
                    text.pop_back();
                const int level = std::atoi(text.c_str());
                if (!from->SetFlySpeedLevel(level)) {
                    fail(from, "Flying speed must be 1x, 2x, 5x, 10x or 20x");
                    return;
                }
                from->SendActionBar("Flying speed: " + std::to_string(level) + "x");
            });
        add({"give"}, "Give items to players", true, {"<targets:targets> <item:item> [count:int]"},
            [say, fail](CommandCtx& c) {
                const int64_t count = c.Has("count") ? c["count"].integer : 1;
                if (count < 1) {
                    fail(c.source, "Integer must not be less than 1, found " + std::to_string(count));
                    return;
                }
                const int stack = static_cast<int>(std::min<int64_t>(count, 2304));
                for (Session* s : c["targets"].players) {
                    const int left = s->Inventory().Give(static_cast<int16_t>(c["item"].index), stack);
                    s->SendInventory();
                    const std::string shown = c["item"].text.substr(c["item"].text.find(':') + 1);
                    say(c.source, "Gave " + std::to_string(stack - left) + " " + shown + " to " + s->Name() +
                                      (left ? " (inventory full)" : ""));
                }
            });
        add({"enchant"}, "Adds an enchantment to a player's selected item", true,
            {"<player:targets> <enchantmentName:enum(protection|fire_protection|feather_falling|blast_protection|"
             "projectile_protection|thorns|respiration|depth_strider|aqua_affinity|sharpness|smite|"
             "bane_of_arthropods|knockback|fire_aspect|looting|efficiency|silk_touch|unbreaking|fortune|power|"
             "punch|flame|infinity|luck_of_the_sea|lure|frost_walker|mending|binding|vanishing|impaling|riptide|"
             "loyalty|channeling|multishot|piercing|quick_charge|soul_speed|swift_sneak|wind_burst|density|"
             "breach)> [level:int]"},
            [say, fail](CommandCtx& c) {
                const int id = EnchantId(c["enchantmentName"].text);
                const int64_t level = c.Has("level") ? c["level"].integer : 1;
                if (id < 0 || level < 1 || level > 255) {
                    fail(c.source, "Invalid enchantment or level");
                    return;
                }
                for (Session* s : c["player"].players) {
                    ItemStack& held = s->Inventory().main[static_cast<size_t>(s->Inventory().selected)];
                    if (held.Empty()) {
                        fail(c.source, s->Name() + " is not holding an item");
                        continue;
                    }
                    bool replaced = false;
                    for (auto& [e, l] : held.enchants)
                        if (e == id) {
                            l = static_cast<int16_t>(level);
                            replaced = true;
                        }
                    if (!replaced)
                        held.enchants.emplace_back(static_cast<int16_t>(id), static_cast<int16_t>(level));
                    held.stackId = s->Inventory().NewStackId();
                    s->SendInventory();
                    say(c.source, "Enchanting succeeded for " + s->Name());
                }
            });
        std::string mobNames;
        for (const MobType& type : MobRegistry::Instance().All()) {
            const std::string id = type.def.id;
            if (!mobNames.empty())
                mobNames += '|';
            mobNames += id.substr(id.find(':') + 1);
        }
        mobNames += "|tnt";
        add({"summon"}, "Summons an entity", true,
            {"<entityType:enum(" + mobNames + ")>", "<entityType:enum(" + mobNames + ")> <spawnPos:pos>"},
            [this, say, fail](CommandCtx& c) {
                double x = SpawnX() + 0.5, y = SpawnY(), z = SpawnZ() + 0.5;
                if (c.source) {
                    x = c.source->X();
                    y = c.source->Y() - 1.62;
                    z = c.source->Z();
                }
                if (c.Has("spawnPos")) {
                    x = c["spawnPos"].pos[0];
                    y = c["spawnPos"].pos[1];
                    z = c["spawnPos"].pos[2];
                }
                // Lit TNT is an entity of its own, not a mob: four seconds of fuse, as in the game.
                Mob* m = c["entityType"].text == "tnt"
                             ? MobsIn(c.source ? c.source->DimensionId() : kOverworld).SpawnPrimedTnt(x, y, z, 80)
                             : MobsIn(c.source ? c.source->DimensionId() : kOverworld)
                                   .Spawn("minecraft:" + c["entityType"].text, x, y, z, true);
                if (!m) {
                    fail(c.source, "Unable to summon entity");
                    return;
                }
                say(c.source, "Summoned new " + c["entityType"].text);
            });
        add({"kill"}, "Kills entities (players, mobs, etc.)", true, {"", "<selector:text>"},
            [this, say, fail](CommandCtx& c) {
                if (c.Has("selector")) {
                    const std::string sel = c["selector"].text;
                    if (!sel.starts_with("@e")) {
                        std::vector<Session*> list;
                        std::string error;
                        if (!ResolveSelector(*this, c.source, sel, list, error) || list.empty()) {
                            fail(c.source, error.empty() ? "No player was found" : error);
                            return;
                        }
                        for (Session* s : list) {
                            s->Kill();
                            say(c.source, "Killed " + s->Name());
                        }
                        return;
                    }
                    std::string type;
                    if (const size_t at = sel.find("type="); at != std::string::npos) {
                        type = sel.substr(at + 5);
                        type = type.substr(0, type.find_first_of(",]"));
                        if (type.starts_with("minecraft:"))
                            type = type.substr(10);
                    }
                    int n = 0;
                    for (int d = 0; d < kDimensionCount; d++)
                        n += MobsIn(d).KillAll(type);
                    if (n == 0)
                        fail(c.source, "No entity was found");
                    else
                        say(c.source, "Killed " + std::to_string(n) + " entities");
                    return;
                }
                std::vector<Session*> list;
                if (c.source)
                    list.push_back(c.source);
                for (Session* s : list) {
                    s->Kill();
                    say(c.source, "Killed " + s->Name());
                }
            });
        add({"xp", "experience"}, "Adds or removes player experience", true,
            {"<amount:string>", "<amount:string> <player:targets>"}, [say, fail](CommandCtx& c) {
                std::string text = c["amount"].text;
                const bool levels = !text.empty() && (text.back() == 'L' || text.back() == 'l');
                if (levels)
                    text.pop_back();
                char* end = nullptr;
                const long amount = std::strtol(text.c_str(), &end, 10);
                if (text.empty() || !end || *end) {
                    fail(c.source, "Expected a number, optionally followed by L: " + c["amount"].text);
                    return;
                }
                std::vector<Session*> list = c.Has("player") ? c["player"].players : std::vector<Session*>{};
                if (!c.Has("player")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no experience; name a player: /xp <amount> <player>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    if (levels) {
                        s->AddLevels(static_cast<int>(amount));
                        say(c.source, (amount >= 0 ? "Gave " : "Took ") + std::to_string(std::labs(amount)) +
                                          " levels " + (amount >= 0 ? "to " : "from ") + s->Name());
                    } else {
                        if (amount < 0) {
                            fail(c.source, "Cannot remove experience points; use levels, for example /xp -5L");
                            return;
                        }
                        s->AddExperience(static_cast<int>(amount));
                        say(c.source, "Gave " + std::to_string(amount) + " experience to " + s->Name());
                    }
                }
            });
        {
            static const char* const kEffects[] = {
                "", "speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage",
                "jump_boost", "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing",
                "invisibility", "blindness", "night_vision", "hunger", "weakness", "poison", "wither", "health_boost",
                "absorption", "saturation", "levitation", "fatal_poison", "conduit_power", "slow_falling", "bad_omen",
                "village_hero", "darkness"};
            std::string names = "clear";
            for (size_t i = 1; i < std::size(kEffects); i++)
                names += std::string("|") + kEffects[i];
            add({"effect"}, "Add or remove status effects", true,
                {"<player:targets> <effect:enum(" + names + ")>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int> <amplifier:int>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int> <amplifier:int> <hideParticles:enum(true|false)>"},
                [say, fail](CommandCtx& c) {
                    const std::string effect = c["effect"].text;
                    int id = 0;
                    for (size_t i = 1; i < std::size(kEffects); i++)
                        if (effect == kEffects[i])
                            id = static_cast<int>(i);
                    for (Session* s : c["player"].players) {
                        if (effect == "clear") {
                            s->ClearEffects();
                            say(c.source, "Took all effects from " + s->Name());
                            continue;
                        }
                        const int64_t seconds = c.Has("seconds") ? c["seconds"].integer : 30;
                        const int64_t amplifier = c.Has("amplifier") ? c["amplifier"].integer : 0;
                        if (amplifier < 0 || amplifier > 255) {
                            fail(c.source, "The amplifier must be between 0 and 255");
                            return;
                        }
                        if (seconds <= 0) {
                            if (s->RemoveEffect(id))
                                say(c.source, "Took " + effect + " from " + s->Name());
                            else
                                fail(c.source, s->Name() + " does not have " + effect);
                            continue;
                        }
                        const bool hide = c.Has("hideParticles") && c["hideParticles"].text == "true";
                        s->AddEffect(id, static_cast<int>(amplifier),
                                     static_cast<int>(std::min<int64_t>(seconds, 1000000) * 20), !hide);
                        say(c.source, "Gave " + effect + " * " + std::to_string(amplifier) + " to " + s->Name() +
                                          " for " + std::to_string(seconds) + " seconds");
                    }
                });
        }
        add({"clear"}, "Remove items from player inventories", true,
            {"", "<targets:targets>", "<targets:targets> <item:item>",
             "<targets:targets> <item:item> <maxCount:int>"},
            [say, fail](CommandCtx& c) {
                std::vector<Session*> list =
                    c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no inventory; name a player: /clear <targets>");
                        return;
                    }
                    list.push_back(c.source);
                }
                const bool filtered = c.Has("item");
                const int16_t wanted = filtered ? static_cast<int16_t>(c["item"].index) : 0;
                const int64_t limit = c.Has("maxCount") ? c["maxCount"].integer : -1;
                for (Session* s : list) {
                    int64_t removed = 0;
                    const auto sweep = [&](ItemStack& slot) {
                        if (slot.Empty() || (filtered && slot.networkId != wanted))
                            return;
                        const int64_t take =
                            limit < 0 ? slot.count
                                      : std::min<int64_t>(slot.count, std::max<int64_t>(0, limit - removed));
                        removed += take;
                        if (limit != 0 && take > 0)
                            slot.count = static_cast<decltype(slot.count)>(slot.count - take);
                        if (slot.count == 0)
                            slot = ItemStack{};
                    };
                    for (ItemStack& slot : s->Inventory().main)
                        sweep(slot);
                    for (ItemStack& slot : s->Inventory().armor)
                        sweep(slot);
                    sweep(s->Inventory().offhand);
                    if (limit != 0 && removed > 0)
                        s->SendInventory();
                    if (removed == 0)
                        fail(c.source, "No items were found on player " + s->Name());
                    else if (limit == 0)
                        say(c.source,
                            "Found " + std::to_string(removed) + " matching item(s) on player " + s->Name());
                    else
                        say(c.source,
                            "Removed " + std::to_string(removed) + " item(s) from player " + s->Name());
                }
            });
    }
}
