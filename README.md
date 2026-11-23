# Perlite

Minecraft: Bedrock Edition server software written in C++23. Perlite runs as a standalone dedicated server for regular Bedrock clients, and the [Perlin](https://github.com/theperlinproject/Perlin) client embeds it for singleplayer worlds. Plugins are WebAssembly modules built against [perlin-api](https://github.com/theperlinproject/perlin-api), the same API Perlin's client plugins use.

Not affiliated with Mojang AB.

| Minecraft | Protocol |
|-----------|----------|
| 1.26.60   | 2225     |

## Building

Needs CMake 3.28+, a C++23 compiler, Python 3, OpenSSL and zlib. The first configure downloads perlin-api, libdatachannel, miniz, wasm3 and LevelDB (the zlib build Bedrock worlds use).

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/perlite --data run
```

| Option | Default | What it does |
|--------|---------|--------------|
| `PERLITE_NETHERNET` | ON | NetherNet transport (WebRTC), the only way Bedrock 26.60+ clients join. Without it only in-process (loopback) connections work. |
| `PERLITE_LEVELDB` | ON (OFF on the web) | Save worlds in the Bedrock LevelDB format. |
| `PERLITE_EMBED` | OFF | Build `perlite_embed`, the server without a wasm runtime, for hosts that bring their own. |
| `PERLITE_BUILD_EXE`, `PERLITE_BUILD_SHARED`, `PERLITE_BUILD_TESTS` | ON at top level | The executable, the shared library with the C API, the tests. |

Point `PERLIN_API_SOURCE_DIR` at a local perlin-api checkout to build against it instead of fetching it.

## Running

Perlite ships with a commented `server.properties` and reads it as it is: there are no questions on the first start. If the file is missing, the default one is written next to the server. Every setting has a comment above it saying what it does, and `./perlite --print-default-properties` prints the default file. The keys are the usual Bedrock server keys, plus a few `perlite-*` keys:

| Key | Default | |
|-----|---------|---|
| `enable-lan-visibility` | `true` | Answer NetherNet LAN discovery on UDP 7551 so the server shows under Friends > LAN Games and can be joined from there. |
| `network-encryption` | `true` | The encryption handshake on every non-local login. Over NetherNet the handshake is sent but the stream stays plain, since DTLS already encrypts it. |
| `texturepack-required` | `false` | Players must accept the server's packs. |
| `enable-packet-rate-limiter` | `true` | Drops clients that flood the server with packets. |
| `perlite-world-storage` | `leveldb` | `leveldb` saves the world in `worlds/<level-name>`; `none` keeps it in memory. |
| `perlite-autosave-seconds` | `300` | How often the world is saved; 0 saves only on stop and `/save`. |
| `perlite-generator` | `vanilla` | `vanilla` or `flat`. |
| `perlite-plugin-dir`, `perlite-block-data-dir`, `perlite-flat-layers`, `perlite-ice-servers`, `perlite-ice-bind-address`, `perlite-auth-root-keys` | | Same as before the split; the old `perlin-*` spellings still load. |

### Ports and joining

Players join with the server's address and `server-port` (19132 by default): the client first sends an HTTP request over **TCP** to that port, then plays over **UDP** on `server-udp-ports` (also 19132 by default). Open both TCP and UDP for that port in your firewall and router. LAN discovery uses **UDP 7551** broadcasts, so it only works on the same subnet.

Connect to `127.0.0.1` or the machine's LAN address (for example `192.168.1.20`), never `0.0.0.0`: that is a "listen on every interface" address for `server-ip`, not one a client can reach.

The server's NetherNet identity key lives in `nethernet-identity.pem` in the data folder. Keep it: a new key makes every player confirm the server again on their next join.

### Logs

The console and `logs/latest.log` use one line per event, `[HH:MM:SS] [thread/LEVEL]: message`: startup, the spawn area being prepared, players joining and leaving (with their address, entity id and position), commands players run, deaths, saves, and a warning when the server cannot keep up. On the next start the previous `latest.log` is compressed to `logs/YYYY-MM-DD-N.log.gz`. `debug-log-packets=true` adds the packet log.

### Vanilla data

Perlite contains no Mojang assets. By running it you accept the [Minecraft EULA](https://www.minecraft.net/eula), which the server also says as the first line of its log. On the first start it downloads the latest Bedrock Dedicated Server from minecraft.net, keeps only its vanilla behavior packs (the `vanilla` pack and the versioned packs stacked on it, merged in version order with their packed `.brarchive` folders unpacked) in `vanilla_behavior_pack/` in the data folder, and deletes the rest of the download. It downloads again when a newer server comes out. Mob collision boxes, scale, size classes, coat variants, health, speed and attack damage come from it, and so do the block shapes, the station recipes, the structure templates and the loot tables. Without it the server still runs, with built-in mob sizes, full-cube block shapes and no template structures. `perlite-download-vanilla-pack=false` turns the download off; `PERLITE_VANILLA_PACK=<folder>` points at a copy you already have.

### Gameplay

Besides building, combat, mobs, containers, crafting and the commands, the world changes by itself around players: random ticks (the `randomtickspeed` game rule) grow crops, stems, sugar cane, cactus, berries, nether wart, cocoa, kelp and amethyst, turn saplings into trees, spread grass and mycelium, dry and wet farmland, decay leaves that lost their logs, and age copper. Digging in survival shows the crack animation to everyone nearby, and its speed follows the tool, Efficiency, Haste, Mining Fatigue, water and being in the air.

Other files in the data folder: `allowlist.json`, `permissions.json`, `banned-players.json`, `banned-ips.json`, `plugins/`, `resource_packs/` and `behavior_packs/` (folders, `.zip` or `.mcpack`, with an optional `<pack>.key` holding the content key).


## The C API

`include/perlite/perlite.h` creates a server, starts it on its own thread or ticks it from the host, runs console commands, and opens in-process loopback connections. Perlin's singleplayer uses it through `perlite_embed`. The `config_json` option overrides `server.properties` keys for an embedded server: `view_distance`, `max_players`, `gamemode`, `seed`, `generator`, `block_data_dir`, `plugin_dir`, `max_threads`, `world_storage`, `level_name` and `autosave_seconds`.