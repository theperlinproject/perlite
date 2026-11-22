#include "server/commands/builtin/BuiltinSupport.hpp"

namespace perlite {
    void Server::RegisterWorldCommands(const CommandRegistrar& r) {
        const auto& say = r.say;
        [[maybe_unused]] const auto& fail = r.fail;
        const auto& add = r.add;
        add({"difficulty"}, "Show or set the difficulty", true,
            {"", "<difficulty:enum(peaceful|easy|normal|hard)>"}, [this, say](CommandCtx& c) {
                if (!c.Has("difficulty")) {
                    say(c.source, std::string("The difficulty is ") + DifficultyName(config.difficulty));
                    return;
                }
                const std::string d = c["difficulty"].text;
                config.difficulty = d == "peaceful" ? 0 : d == "easy" ? 1 : d == "normal" ? 2 : 3;
                for (Session* s : InGamePlayers())
                    s->SendDifficulty(config.difficulty);
                say(c.source,
                    std::string("The difficulty has been set to ") + DifficultyName(config.difficulty));
            });
        add({"changesetting"}, "Change a server setting", true,
            {"allow-cheats <value:enum(true|false)>", "difficulty <value:enum(peaceful|easy|normal|hard)>"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0) {
                    config.allowCheats = c["value"].text == "true";
                    say(c.source, "allow-cheats is now " + c["value"].text);
                } else {
                    const std::string d = c["value"].text;
                    config.difficulty = d == "peaceful" ? 0 : d == "easy" ? 1 : d == "normal" ? 2 : 3;
                    for (Session* s : InGamePlayers())
                        s->SendDifficulty(config.difficulty);
                    say(c.source, "difficulty is now " + d);
                }
            });
        add({"time"}, "Change or query the world time", true,
            {"set <value:enum(day|noon|night|midnight)>", "set <ticks:int>", "add <ticks:int>",
             "query <what:enum(daytime|gametime|day)>"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0 || c.overload == 1) {
                    int64_t t = 0;
                    if (c.overload == 0) {
                        const std::string v = c["value"].text;
                        t = v == "day" ? 1000 : v == "noon" ? 6000 : v == "night" ? 13000 : 18000;
                    } else {
                        t = c["ticks"].integer;
                    }
                    SetDayTime((dayTime / 24000) * 24000 + ((t % 24000) + 24000) % 24000);
                    say(c.source, "Set the time to " + std::to_string(t));
                } else if (c.overload == 2) {
                    SetDayTime(dayTime + c["ticks"].integer);
                    say(c.source, "Set the time to " + std::to_string(dayTime % 24000));
                } else {
                    const std::string w = c["what"].text;
                    if (w == "daytime")
                        say(c.source, "The time is " + std::to_string(dayTime % 24000));
                    else if (w == "gametime")
                        say(c.source, "The time is " + std::to_string(TickCount()));
                    else
                        say(c.source, "The time is " + std::to_string(dayTime / 24000));
                }
            });
        add({"weather"}, "Set the weather", true, {"<kind:enum(clear|rain|thunder)> [duration:int]"},
            [this, say, fail](CommandCtx& c) {
                const std::string kind = c["kind"].text;
                if (c.Has("duration") && (c["duration"].integer < 1 || c["duration"].integer > 1000000)) {
                    fail(c.source,
                         "Integer must not be less than 1, found " + std::to_string(c["duration"].integer));
                    return;
                }
                const int ticks = c.Has("duration") ? static_cast<int>(c["duration"].integer) * 20
                                                    : (kind == "clear" ? 0 : 6000);
                SetWeather(kind, ticks);
                say(c.source, kind == "clear"  ? "Changed the weather to clear"
                              : kind == "rain" ? "Changed the weather to rain"
                                               : "Changed the weather to rain and thunder");
            });
        add({"gamerule"}, "Show or change a game rule", true,
            {"<rule:string>", "<rule:string> <value:string>"}, [this, say, fail](CommandCtx& c) {
                const auto it = gameRules.find(str::Lower(c["rule"].text));
                if (it == gameRules.end()) {
                    fail(c.source, "Incorrect argument for command");
                    fail(c.source, "/gamerule " + c["rule"].text + "<--[HERE]");
                    return;
                }
                const GameRule& rule = it->second;
                const auto shown = [&](int v) {
                    return rule.boolean ? std::string(v ? "true" : "false") : std::to_string(v);
                };
                if (!c.Has("value")) {
                    say(c.source, "Game rule " + rule.display + " is currently set to: " + shown(rule.value));
                    return;
                }
                const std::string v = str::Lower(c["value"].text);
                int value = 0;
                if (rule.boolean) {
                    if (v != "true" && v != "false") {
                        fail(c.source, "Invalid boolean, expected 'true' or 'false' but found '" +
                                           c["value"].text + "'");
                        return;
                    }
                    value = v == "true";
                } else {
                    char* end = nullptr;
                    const long parsed = std::strtol(v.c_str(), &end, 10);
                    if (v.empty() || *end != '\0') {
                        fail(c.source, "Expected integer");
                        return;
                    }
                    value = static_cast<int>(parsed);
                }
                SetRule(it->first, value);
                say(c.source, "Game rule " + rule.display + " has been updated to " + shown(rule.value));
            });
        add({"setworldspawn"}, "Set the world spawn point", true, {"", "<position:blockpos>"},
            [this, say](CommandCtx& c) {
                int x, y, z;
                if (c.Has("position")) {
                    x = static_cast<int>(c["position"].pos[0]);
                    y = static_cast<int>(c["position"].pos[1]);
                    z = static_cast<int>(c["position"].pos[2]);
                } else {
                    x = static_cast<int>(std::floor(c.source ? c.source->X() : SpawnX()));
                    y = static_cast<int>(std::floor(c.source ? c.source->Y() - 1.62 : SpawnY()));
                    z = static_cast<int>(std::floor(c.source ? c.source->Z() : SpawnZ()));
                }
                SetWorldSpawn(x, y, z);
                say(c.source, "Set the world spawn point to " + BlockPosText(x, y, z) + " [0.0]");
            });
        add({"spawnpoint"}, "Set a player's spawn point", true,
            {"", "<targets:targets>", "<targets:targets> <position:blockpos>"}, [say, fail](CommandCtx& c) {
                std::vector<Session*> list =
                    c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no spawn point; name a player: /spawnpoint <targets>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    const int x = c.Has("position") ? static_cast<int>(c["position"].pos[0])
                                                    : static_cast<int>(std::floor(s->X()));
                    const int y = c.Has("position") ? static_cast<int>(c["position"].pos[1])
                                                    : static_cast<int>(std::floor(s->Y() - 1.62));
                    const int z = c.Has("position") ? static_cast<int>(c["position"].pos[2])
                                                    : static_cast<int>(std::floor(s->Z()));
                    s->SendSpawnPosition(x, y, z, false);
                    say(c.source, "Set spawn point to " + BlockPosText(x, y, z) + " for " + s->Name());
                }
            });

        const auto loaded = [this](int dim, int x, int z) { return World(dim).Peek({x >> 4, z >> 4}) != nullptr; };
        const auto registryOf = [](const ArgValue& v) -> uint16_t {
            if (v.block >= 0)
                return static_cast<uint16_t>(v.block);
            const CatalogItem* item = BlockCatalog::Instance().ItemById(static_cast<int16_t>(v.index));
            return item ? BlockCatalog::Instance().RegistryId(item->blockHash) : BlockRegistry::kAir;
        };
        add({"setblock"}, "Change a block", true,
            {"<position:blockpos> <block:block> [mode:enum(destroy|keep|replace)]"},
            [this, say, fail, loaded, registryOf](CommandCtx& c) {
                const int x = static_cast<int>(c["position"].pos[0]),
                          y = static_cast<int>(c["position"].pos[1]),
                          z = static_cast<int>(c["position"].pos[2]);
                const int dim = CommandDimension(c.source);
                const DimensionRange range = RangeOf(dim);
                if (y < range.minY || y > range.MaxY()) {
                    fail(c.source, "That position is outside the world");
                    return;
                }
                if (!loaded(dim, x, z)) {
                    fail(c.source, "That position is not loaded");
                    return;
                }
                const uint16_t block = registryOf(c["block"]);
                const std::string mode = c.Has("mode") ? c["mode"].text : "replace";
                if (mode == "keep" && World(dim).GetBlock(x, y, z) != BlockRegistry::kAir) {
                    fail(c.source, "Could not set the block");
                    return;
                }
                ApplyBlock(dim, x, y, z, block);
                say(c.source, "Changed the block at " + BlockPosText(x, y, z));
            });
        // Copies the blocks of one region to another; masked skips air, move clears the source.
        add({"clone"}, "Copy blocks from one region to another", true,
            {"<begin:blockpos> <end:blockpos> <destination:blockpos> [mask:enum(replace|masked)] "
             "[clone:enum(normal|force|move)]",
             "<begin:blockpos> <end:blockpos> <destination:blockpos> filtered <clone:enum(normal|force|move)> "
             "<tile:block>"},
            [this, say, fail, loaded, registryOf](CommandCtx& c) {
                int lo[3], hi[3], to[3];
                for (int k = 0; k < 3; k++) {
                    const size_t i = static_cast<size_t>(k);
                    const int a = static_cast<int>(c["begin"].pos[i]), b = static_cast<int>(c["end"].pos[i]);
                    lo[k] = std::min(a, b);
                    hi[k] = std::max(a, b);
                    to[k] = static_cast<int>(c["destination"].pos[i]);
                }
                const int64_t volume =
                    static_cast<int64_t>(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
                if (volume > 32768) {
                    fail(c.source, "Too many blocks in the specified area (maximum 32768, specified " +
                                       std::to_string(volume) + ")");
                    return;
                }
                const std::string mask = c.overload == 1 ? "filtered" : c.Has("mask") ? c["mask"].text : "replace";
                const std::string how = c.Has("clone") ? c["clone"].text : "normal";
                const bool overlap = to[0] <= hi[0] && to[0] + (hi[0] - lo[0]) >= lo[0] && to[1] <= hi[1] &&
                                     to[1] + (hi[1] - lo[1]) >= lo[1] && to[2] <= hi[2] &&
                                     to[2] + (hi[2] - lo[2]) >= lo[2];
                if (overlap && how != "force") {
                    fail(c.source, "Source and destination can not overlap");
                    return;
                }
                const int dim = CommandDimension(c.source);
                const DimensionRange range = RangeOf(dim);
                if (lo[1] < range.minY || hi[1] > range.MaxY() || to[1] < range.minY ||
                    to[1] + (hi[1] - lo[1]) > range.MaxY()) {
                    fail(c.source, "That position is outside the world");
                    return;
                }
                for (int pass = 0; pass < 2; pass++) {
                    const int ox = pass ? to[0] : lo[0], oz = pass ? to[2] : lo[2];
                    for (int x = ox >> 4; x <= (ox + hi[0] - lo[0]) >> 4; x++)
                        for (int z = oz >> 4; z <= (oz + hi[2] - lo[2]) >> 4; z++)
                            if (!loaded(dim, x * 16, z * 16)) {
                                fail(c.source, "That position is not loaded");
                                return;
                            }
                }
                const uint16_t tile = mask == "filtered" ? registryOf(c["tile"]) : 0;
                struct Copy {
                    int dx, dy, dz;
                    uint16_t block;
                };
                std::vector<Copy> copies;
                for (int x = lo[0]; x <= hi[0]; x++)
                    for (int y = lo[1]; y <= hi[1]; y++)
                        for (int z = lo[2]; z <= hi[2]; z++) {
                            const uint16_t b = World(dim).GetBlock(x, y, z);
                            if (mask == "masked" && b == BlockRegistry::kAir)
                                continue;
                            if (mask == "filtered" && b != tile)
                                continue;
                            copies.push_back({x - lo[0], y - lo[1], z - lo[2], b});
                        }
                if (how == "move")
                    for (const Copy& k : copies)
                        ApplyBlock(dim, lo[0] + k.dx, lo[1] + k.dy, lo[2] + k.dz, BlockRegistry::kAir);
                int changed = 0;
                for (const Copy& k : copies) {
                    const int x = to[0] + k.dx, y = to[1] + k.dy, z = to[2] + k.dz;
                    if (World(dim).GetBlock(x, y, z) == k.block)
                        continue;
                    ApplyBlock(dim, x, y, z, k.block);
                    changed++;
                }
                if (changed == 0)
                    fail(c.source, "No blocks cloned");
                else
                    say(c.source, std::to_string(changed) + " blocks cloned");
            });

        // Compares two regions block by block.
        add({"testforblocks"}, "Tests whether the blocks in two regions match", true,
            {"<begin:blockpos> <end:blockpos> <destination:blockpos> [mode:enum(all|masked)]"},
            [this, say, fail](CommandCtx& c) {
                int lo[3], hi[3], to[3];
                for (int k = 0; k < 3; k++) {
                    const size_t i = static_cast<size_t>(k);
                    const int a = static_cast<int>(c["begin"].pos[i]), b = static_cast<int>(c["end"].pos[i]);
                    lo[k] = std::min(a, b);
                    hi[k] = std::max(a, b);
                    to[k] = static_cast<int>(c["destination"].pos[i]);
                }
                const int64_t volume =
                    static_cast<int64_t>(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
                if (volume > 524288) {
                    fail(c.source, "Too many blocks in the specified area (maximum 524288, specified " +
                                       std::to_string(volume) + ")");
                    return;
                }
                const bool masked = c.Has("mode") && c["mode"].text == "masked";
                const int dim = CommandDimension(c.source);
                int64_t compared = 0;
                for (int x = lo[0]; x <= hi[0]; x++)
                    for (int y = lo[1]; y <= hi[1]; y++)
                        for (int z = lo[2]; z <= hi[2]; z++) {
                            const uint16_t a = World(dim).GetBlock(x, y, z);
                            if (masked && a == BlockRegistry::kAir)
                                continue;
                            const uint16_t b =
                                World(dim).GetBlock(to[0] + x - lo[0], to[1] + y - lo[1], to[2] + z - lo[2]);
                            if (a != b) {
                                fail(c.source, "Source and destination are not the same");
                                return;
                            }
                            compared++;
                        }
                say(c.source, std::to_string(compared) + " blocks compared");
            });

        add({"fill"}, "Fill a region with a block", true,
            {"<from:blockpos> <to:blockpos> <block:block> [mode:enum(destroy|hollow|keep|outline|replace)]",
             "<from:blockpos> <to:blockpos> <block:block> replace <filter:block>"},
            [this, say, fail, loaded, registryOf](CommandCtx& c) {
                int lo[3], hi[3];
                for (int k = 0; k < 3; k++) {
                    const int a = static_cast<int>(c["from"].pos[static_cast<size_t>(k)]),
                              b = static_cast<int>(c["to"].pos[static_cast<size_t>(k)]);
                    lo[k] = std::min(a, b);
                    hi[k] = std::max(a, b);
                }
                const int64_t volume =
                    static_cast<int64_t>(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
                if (volume > 32768) {
                    fail(c.source, "Too many blocks in the specified area (maximum 32768, specified " +
                                       std::to_string(volume) + ")");
                    return;
                }
                const int dim = CommandDimension(c.source);
                const DimensionRange range = RangeOf(dim);
                if (lo[1] < range.minY || hi[1] > range.MaxY()) {
                    fail(c.source, "That position is outside the world");
                    return;
                }
                for (int x = lo[0] >> 4; x <= hi[0] >> 4; x++)
                    for (int z = lo[2] >> 4; z <= hi[2] >> 4; z++)
                        if (!loaded(dim, x * 16, z * 16)) {
                            fail(c.source, "That position is not loaded");
                            return;
                        }
                const uint16_t block = registryOf(c["block"]);
                const std::string mode =
                    c.overload == 1 ? "replace" : (c.Has("mode") ? c["mode"].text : "replace");
                const bool filtered = c.overload == 1;
                const uint16_t filter = filtered ? registryOf(c["filter"]) : 0;
                int changed = 0;
                for (int x = lo[0]; x <= hi[0]; x++)
                    for (int y = lo[1]; y <= hi[1]; y++)
                        for (int z = lo[2]; z <= hi[2]; z++) {
                            const bool edge = x == lo[0] || x == hi[0] || y == lo[1] || y == hi[1] ||
                                              z == lo[2] || z == hi[2];
                            const uint16_t current = World(dim).GetBlock(x, y, z);
                            uint16_t put = block;
                            if (mode == "keep" && current != BlockRegistry::kAir)
                                continue;
                            if (mode == "outline" && !edge)
                                continue;
                            if (mode == "hollow" && !edge)
                                put = BlockRegistry::kAir;
                            if (filtered && current != filter)
                                continue;
                            if (current == put)
                                continue;
                            ApplyBlock(dim, x, y, z, put);
                            changed++;
                        }
                if (changed == 0)
                    fail(c.source, "No blocks were filled");
                else
                    say(c.source, std::to_string(changed) + " blocks filled");
            });

        add({"locate"}, "Find the nearest biome or structure", true,
            {"biome <biome:biome>", "structure <structure:string>"}, [this, say, fail](CommandCtx& c) {
                if (c.overload == 1) {
                    fail(c.source, "Could not find a structure of type \"" + c["structure"].text +
                                       "\": this server does not generate structures yet");
                    return;
                }
                TerrainBackend& backend = World(CommandDimension(c.source)).Backend();
                if (!backend.HasBiomes()) {
                    fail(c.source, "This world has no biomes to locate (it is a flat world)");
                    return;
                }
                const uint64_t id = c.source ? c.source->Id() : 0;
                if (locating.contains(id)) {
                    fail(c.source, "A search is already running; it answers when it is done");
                    return;
                }
                locating.insert(id);
                const CommandOrigin* origin = ActiveOrigin();
                const int fromX = static_cast<int>(std::floor(origin ? origin->x : c.source ? c.source->X() : SpawnX()));
                const int fromZ = static_cast<int>(std::floor(origin ? origin->z : c.source ? c.source->Z() : SpawnZ()));
                const int biome = c["biome"].index;
                const std::string name = c["biome"].text;
                say(c.source, "Searching for " + name + " ...");
                locateThreads.emplace_back([this, &backend, id, biome, name, fromX, fromZ] {
                    int x = 0, y = 0, z = 0;
                    const bool found = backend.LocateBiome(biome, fromX, fromZ, 6400, locateCancel, x, y, z);
                    if (locateCancel)
                        return;
                    std::string text;
                    if (found) {
                        const int distance = static_cast<int>(std::lround(
                            std::hypot(static_cast<double>(x - fromX), static_cast<double>(z - fromZ))));
                        text = "The nearest " + name + " is at [" + BlockPosText(x, y, z) + "] (" +
                               std::to_string(distance) + " blocks away)";
                    } else {
                        text = "§cCould not find a biome of type \"" + name + "\" within 6400 blocks";
                    }
                    std::lock_guard<std::mutex> lock(locateMutex);
                    locateDone.emplace_back(id, std::move(text));
                });
            });

        add({"execute"}, "Execute another command", true, {"<subcommands:text>"},
            [this](CommandCtx& c) { RunExecute(c.source, c["subcommands"].text); });

        add({"tick"}, "Query or control the game's tick rate", true,
            {"query", "rate <rate:float>", "freeze", "unfreeze", "step", "step <time:string>", "sprint stop",
             "sprint <time:string>"},
            [this](CommandCtx& c) { RunTickCommand(c.source, c.raw); });
    }
}
