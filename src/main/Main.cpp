#include "common/TaskGroup.hpp"
#include "server/Config.hpp"
#include "common/Log.hpp"
#include "server/Server.hpp"
#include "world/level/block/BlockCatalog.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace perlite;

namespace {
    std::atomic<Server*> g_server{nullptr};

    void OnSignal(int) {
        if (Server* s = g_server.load())
            s->RequestStop();
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string dir = ".";
    int port = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc)
            dir = argv[++i];
        else if (a == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (a == "--no-wizard")
            continue; // accepted for old scripts; there is no wizard any more
        else if (a == "--print-default-properties") {
            std::cout << ServerConfig::FirstRun().PropertiesText();
            return 0;
        } else if (a == "--help" || a == "-h") {
            std::cout << "perlite [--data <dir>] [--port <n>]\n"
                         "  reads <dir>/server.properties (a commented default is written if it is missing),\n"
                         "  allowlist.json, permissions.json and plugins from <dir>/plugins\n";
            return 0;
        }
    }
    std::filesystem::create_directories(dir);
    SetLogThreadName("Server thread");
    OpenLogFile(dir + "/logs");
    LogInfo("Starting Minecraft: Bedrock Edition server version 1.26.60 (protocol 2225)");
    LogInfo("Loading properties");

    const std::string propsPath = dir + "/server.properties";
    const bool hadProps = std::filesystem::exists(propsPath);
    const bool hadLegacy = std::filesystem::exists(dir + "/server.json");
    std::string error;
    ServerConfig config = ServerConfig::LoadDirectory(dir, &error);
    if (!error.empty()) {
        LogError(error);
        return 2;
    }
    if (!hadProps && !hadLegacy) {
        config = ServerConfig::FirstRun();
        if (config.SaveProperties(propsPath))
            LogInfo("No server.properties found, wrote the default one to " + propsPath);
        else
            LogWarn("Could not write " + propsPath + "; starting with the default settings");
    }
    config.dataDir = dir;

    if (config.blockDataDir.empty() && argc > 0) {
        const std::filesystem::path beside = std::filesystem::path(argv[0]).parent_path() / "data";
        if (!BlockCatalog::FindFile(beside.string(), "block_states.json").empty())
            config.blockDataDir = beside.string();
    }
    if (config.pluginDir.empty())
        config.pluginDir = dir + "/plugins";
    if (port > 0)
        config.port = static_cast<uint16_t>(port);
    std::filesystem::create_directories(config.pluginDir);

    Server server(config);
    if (!server.Start(error)) {
        LogError("Cannot start: " + error);
        return 1;
    }
    g_server = &server;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    struct ConsoleInput {
        std::mutex mutex;
        std::vector<std::string> lines;
    };
    const auto input = std::make_shared<ConsoleInput>();
    std::thread([input] {
        SetLogThreadName("Console");
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard lock(input->mutex);
            input->lines.push_back(line);
        }
    }).detach();

    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (!server.StopRequested()) {
        {
            std::vector<std::string> pending;
            {
                std::lock_guard lock(input->mutex);
                pending.swap(input->lines);
            }
            for (const std::string& l : pending)
                server.RunConsoleCommand(l);
        }
        server.Tick();
        next += server.TickDelay();
        const auto now = clock::now();
        if (next < now) {
            server.NoteLag(std::chrono::duration_cast<std::chrono::milliseconds>(now - next));
            next = now;
        } else
            std::this_thread::sleep_until(next);
    }
    g_server = nullptr;
    server.Stop();
    perlite::ProcessTasks().Shutdown();
    return 0;
}
