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
| `PERLITE_NETHERNET` | ON | NetherNet transport (WebRTC). Bedrock 1.26 clients join over NetherNet. |
| `PERLITE_LEVELDB` | ON (OFF on the web) | Save worlds in the Bedrock LevelDB format. |
| `PERLITE_GPU` | OFF | Generate terrain on the GPU through conduit. |
| `PERLITE_EMBED` | OFF | Build `perlite_embed`, the server without a wasm runtime, for hosts that bring their own. |
| `PERLITE_BUILD_EXE`, `PERLITE_BUILD_SHARED`, `PERLITE_BUILD_TESTS` | ON at top level | The executable, the shared library with the C API, the tests. |

Point `PERLIN_API_SOURCE_DIR` at a local perlin-api checkout to build against it instead of fetching it.

## Running

On the first start in a terminal, a short setup wizard asks for the server name, game mode, difficulty, player limit, port, world name and seed, then writes `server.properties` (`--no-wizard` skips it). The file uses the same keys as Bedrock Dedicated Server, plus a few `perlite-*` keys:

| Key | Default | |
|-----|---------|---|
| `transport` | `nethernet` | `nethernet` (what current clients use), `raknet` (older clients and LAN tools), or `both` (RakNet on the game port, WebRTC on port + 2). |
| `network-encryption` | `true` | The encryption handshake on every non-local login. Over NetherNet the handshake is sent but the stream stays plain, as Bedrock Dedicated Server does, since DTLS already encrypts it. |
| `texturepack-required` | `false` | Players must accept the server's packs. |
| `enable-packet-rate-limiter` | `true` | Drops clients that flood the server with packets. |
| `perlite-world-storage` | `leveldb` | `leveldb` saves the world in `worlds/<level-name>`; `none` keeps it in memory. |
| `perlite-autosave-seconds` | `300` | How often the world is saved; 0 saves only on stop and `/save`. |
| `perlite-generator` | `vanilla` | `vanilla` or `flat`. |
| `perlite-terrain-backend` | `auto` | `cpu`, `gpu` or `auto`. |
| `perlite-plugin-dir`, `perlite-block-data-dir`, `perlite-flat-layers`, `perlite-ice-servers`, `perlite-ice-bind-address`, `perlite-auth-root-keys` | | Same as before the split; the old `perlin-*` spellings still load. |

Other files in the data folder: `allowlist.json`, `permissions.json`, `banned-players.json`, `banned-ips.json`, `plugins/`, `resource_packs/` and `behavior_packs/` (folders, `.zip` or `.mcpack`, with an optional `<pack>.key` holding the content key).


## The C API

`include/perlite/perlite.h` creates a server, starts it on its own thread or ticks it from the host, runs console commands, and opens in-process loopback connections. Perlin's singleplayer uses it through `perlite_embed`. The `config_json` option overrides `server.properties` keys for an embedded server: `view_distance`, `max_players`, `gamemode`, `seed`, `generator`, `terrain_backend`, `block_data_dir`, `plugin_dir`, `max_threads`, `world_storage`, `level_name` and `autosave_seconds`.