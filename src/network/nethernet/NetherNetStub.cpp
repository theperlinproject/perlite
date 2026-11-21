#include "server/Server.hpp"

namespace perlite {
    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig&, std::function<std::string()>) {
        return nullptr;
    }
}
