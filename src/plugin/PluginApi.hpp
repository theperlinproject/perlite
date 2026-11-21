#pragma once
#include "perlin/abi.hpp"
#include "perlin/gen/server_service.hpp"

#include <cstdint>
#include <string>

namespace perlin::mods {}

namespace perlite {
    namespace abi = ::perlin::abi;
    namespace mods = ::perlin::mods;

    struct PluginCall {
        const std::string& plugin;
        const std::string& dataDir;
    };

    class PluginApi : public abi::host::ServerService<PluginCall> {
    public:
        virtual void RegisterCommand(const std::string& plugin, const std::string& name,
                                     const std::string& description) = 0;
        virtual void UnregisterCommand(const std::string&, const std::string&) {}
    };
}
