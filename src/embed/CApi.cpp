#include <algorithm>
#include "perlite/perlite.h"

#include "common/Json.hpp"
#include "common/Log.hpp"
#include "server/Server.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace perlite;

struct perlite_server {
    std::unique_ptr<Server> server;
    std::thread thread;
    std::atomic<bool> asyncRunning{false};

    std::atomic<bool> finished{false};
    std::atomic<bool> crashed{false};
};

struct perlite_conn {
    std::shared_ptr<LoopbackListener::Endpoint> endpoint;

    std::vector<uint8_t> held;
    bool hasHeld = false;
};

namespace {
    thread_local std::string g_error;
}

extern "C" {

PERLITE_API uint32_t perlite_abi_version(void) {
    return 1;
}

PERLITE_API void perlite_set_log(perlite_log_fn fn, void* user) {
    if (!fn) {
        SetLogSink(nullptr);
        return;
    }
    SetLogSink([fn, user](LogLevel level, const std::string& text) {
        fn(static_cast<int>(level), text.c_str(), user);
    });
}

PERLITE_API const char* perlite_last_error(void) {
    return g_error.c_str();
}

PERLITE_API perlite_server* perlite_create(const perlite_options* options) {
    try {
        ServerConfig config;
        std::string dir = options && options->data_dir ? options->data_dir : ".";
        std::string error;
        config = ServerConfig::LoadDirectory(dir, &error);
        if (!error.empty())
            LogWarn(error);
        config.dataDir = dir;
        if (config.pluginDir.empty())
            config.pluginDir = dir + "/plugins";
        if (options && options->config_json) {

            json::Value over;
            if (json::Parse(options->config_json, over, &error)) {
                if (over["name"].IsString())
                    config.name = over["name"].text;
                if (over["port"].Int(0) > 0)
                    config.port = static_cast<uint16_t>(over["port"].Int());
                if (over["view_distance"].Int(0) > 0)
                    config.viewDistance = static_cast<uint32_t>(over["view_distance"].Int());
                if (over["max_players"].Int(0) > 0)
                    config.maxPlayers = static_cast<uint32_t>(over["max_players"].Int());
                if (over["terrain_backend"].IsString())
                    config.terrainBackend = over["terrain_backend"].text;
                if (const int64_t gm = over["gamemode"].Int(-1); gm == 0 || gm == 1 || gm == 2 || gm == 6)
                    config.gameMode = static_cast<int>(gm);
                if (over["plugin_dir"].IsString())
                    config.pluginDir = over["plugin_dir"].text;
                if (over["generator"].IsString())
                    config.generator = over["generator"].text;
                if (over["block_data_dir"].IsString())
                    config.blockDataDir = over["block_data_dir"].text;
                if (over["max_threads"].Int(0) > 0)
                    config.maxThreads =
                        static_cast<decltype(config.maxThreads)>(std::clamp<int64_t>(over["max_threads"].Int(), 1, 256));
                if (over["world_storage"].IsString())
                    config.worldStorage = over["world_storage"].text;
                if (over["level_name"].IsString())
                    config.levelName = over["level_name"].text;
                if (over["autosave_seconds"].kind == json::Value::Kind::Number)
                    config.autosaveSeconds = static_cast<uint32_t>(std::clamp<int64_t>(over["autosave_seconds"].Int(), 0, 86400));
                if (over["seed"].kind == json::Value::Kind::Number) {
                    config.seed = static_cast<uint64_t>(over["seed"].Int());
                    config.randomSeed = false;
                }
            } else {
                g_error = "config_json: " + error;
                return nullptr;
            }
        }
        if (options && options->loopback_only)
            config.loopbackOnly = true;
        auto* s = new perlite_server();
        s->server = std::make_unique<Server>(std::move(config));
        return s;
    } catch (const std::exception& e) {
        g_error = e.what();
        return nullptr;
    }
}

PERLITE_API int perlite_start(perlite_server* s) {
    if (!s)
        return -1;
    std::string error;
    if (!s->server->Start(error)) {
        g_error = error;
        return -1;
    }
    return 0;
}

PERLITE_API int perlite_run_async(perlite_server* s) {
    if (!s || s->asyncRunning.exchange(true))
        return -1;
    s->thread = std::thread([s] {
        try {
            s->server->Run();
        } catch (const std::exception& e) {

            LogError(std::string("server crashed: ") + e.what());
            s->crashed = true;
        } catch (...) {
            LogError("server crashed");
            s->crashed = true;
        }
        s->finished = true;
    });
    return 0;
}

PERLITE_API int perlite_is_running(perlite_server* s) {
    return s && s->asyncRunning && !s->finished ? 1 : 0;
}

PERLITE_API void perlite_tick(perlite_server* s) {
    if (s)
        s->server->Tick();
}

PERLITE_API void perlite_stop(perlite_server* s) {
    if (!s)
        return;
    s->server->RequestStop();
    if (s->thread.joinable())
        s->thread.join();
    s->asyncRunning = false;
    s->server->Stop();
}

PERLITE_API void perlite_destroy(perlite_server* s) {
    if (!s)
        return;
    perlite_stop(s);
    delete s;
}

PERLITE_API void perlite_command(perlite_server* s, const char* line) {
    if (s && line)
        s->server->RunConsoleCommand(line);
}

PERLITE_API perlite_conn* perlite_connect_loopback(perlite_server* s) {
    if (!s)
        return nullptr;
    auto endpoint = s->server->Loopback().Connect();
    if (!endpoint)
        return nullptr;
    auto* c = new perlite_conn();
    c->endpoint = std::move(endpoint);
    return c;
}

PERLITE_API int perlite_conn_send(perlite_conn* c, const uint8_t* data, size_t size) {
    return c && c->endpoint->Send(data, size) ? 1 : 0;
}

PERLITE_API int perlite_conn_receive(perlite_conn* c, uint8_t* buf, size_t capacity, size_t* size) {
    if (!c)
        return 0;
    if (!c->hasHeld) {
        if (!c->endpoint->Receive(c->held))
            return 0;
        c->hasHeld = true;
    }
    if (size)
        *size = c->held.size();
    if (c->held.size() > capacity)
        return -1;
    std::copy(c->held.begin(), c->held.end(), buf);
    c->hasHeld = false;
    return 1;
}

PERLITE_API int perlite_conn_alive(perlite_conn* c) {
    return c && c->endpoint->Alive() ? 1 : 0;
}

PERLITE_API void perlite_conn_close(perlite_conn* c) {
    if (!c)
        return;
    c->endpoint->Close();
    delete c;
}
}
