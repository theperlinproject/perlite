#include "main/SetupWizard.hpp"

#include "common/Strings.hpp"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

namespace perlite {
    namespace {
        std::string Ask(const std::string& question, const std::string& fallback) {
            std::cout << question << " [" << fallback << "]: " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line))
                return fallback;
            line = str::Trim(line);
            return line.empty() ? fallback : line;
        }

        bool AskBool(const std::string& question, bool fallback) {
            for (;;) {
                const std::string a = str::Lower(Ask(question + " (yes/no)", fallback ? "yes" : "no"));
                if (a == "y" || a == "yes" || a == "true")
                    return true;
                if (a == "n" || a == "no" || a == "false")
                    return false;
                std::cout << "Please answer yes or no.\n";
            }
        }
    }

    bool InteractiveConsole() {
#ifdef _WIN32
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(STDIN_FILENO) != 0;
#endif
    }

    void RunSetupWizard(ServerConfig& config) {
        std::cout << "\nWelcome to Perlite! Answer a few questions to write server.properties "
                     "(press Enter to keep the default).\n\n";
        for (;;) {
            std::string warning;
            config.SetProperty("server-name", Ask("Server name", config.name), &warning);
            if (warning.empty())
                break;
            std::cout << warning << "\n";
        }
        const auto property = [&](const std::string& key, const std::string& question,
                                  const std::string& now) {
            for (;;) {
                std::string warning;
                config.SetProperty(key, Ask(question, now), &warning);
                if (warning.empty())
                    return;
                std::cout << warning << "\n";
            }
        };
        property("gamemode", "Game mode (survival, creative, adventure)",
                 config.gameMode == 1   ? "creative"
                 : config.gameMode == 2 ? "adventure"
                                        : "survival");
        property("difficulty", "Difficulty (peaceful, easy, normal, hard)",
                 config.difficulty == 0   ? "peaceful"
                 : config.difficulty == 2 ? "normal"
                 : config.difficulty == 3 ? "hard"
                                          : "easy");
        property("max-players", "Maximum players", std::to_string(config.maxPlayers));
        property("server-port", "Port", std::to_string(config.port));
        property("level-name", "World name", config.levelName);
        property("level-seed", "World seed (empty for random)", config.levelSeedText);
        config.xboxAuth = AskBool("Require Xbox Live sign-in (online mode)", config.xboxAuth);
        config.whitelist = AskBool("Only let players on the allowlist join", config.whitelist);
        config.udpPortsSet = true;
        config.udpPortBegin = config.udpPortEnd = config.port;
        std::cout << "\n";
    }
}
