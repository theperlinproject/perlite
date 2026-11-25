#include "common/Log.hpp"
#include "world/level/levelgen/Generator.hpp"
#include "world/level/levelgen/bedrock/Overworld.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"

namespace perlite {
    BackendChoice CreateTerrainBackend(const ServerConfig& config) {
        BackendChoice out;
        if (config.generator == "vanilla") {
            std::string error;
            out.backend = vanilla::VanillaCpuBackend::Create(config, error);
            out.note = out.backend ? "java terrain port" : error;
            return out;
        }
        if (config.generator == "bedrock") {
            std::string error;
            out.backend = bedrock::OverworldBackend::Create(config, error);
            if (out.backend) {
                out.note = "bedrock terrain";
                return out;
            }
            LogWarn("the Bedrock generator could not start (" + error + "); using the Java terrain port");
            out.backend = vanilla::VanillaCpuBackend::Create(config, error);
            out.note = out.backend ? "java terrain port" : error;
            return out;
        }
        out.backend = std::make_unique<FlatCpuBackend>(config);
        out.note = "flat";
        return out;
    }
}
