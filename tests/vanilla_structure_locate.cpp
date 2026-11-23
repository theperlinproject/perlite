// Where structures are, against the reference server: reads lines "type x z refX refZ" (or
// "type x z none") made from the Bedrock Dedicated Server's /locate structure answers, locates the
// same type from the same place with Perlite's generator and the same seed, and reports how many
// land in the same chunk. Not run by ctest (it needs the reference answers and the vanilla pack):
//
//   vanilla_structure_locate <block data dir> <vanilla pack> <seed> <answers file>
#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"
#include "world/level/structure/Structures.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace perlite;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: vanilla_structure_locate <block data dir> <pack> <seed> <answers>\n");
        return 2;
    }
    std::string error;
    if (!BlockCatalog::Instance().Load(BlockCatalog::FindFile(argv[1], "block_states.json"),
                                       BlockCatalog::FindFile(argv[1], "block_destroy.tsv"), error)) {
        std::fprintf(stderr, "block data: %s\n", error.c_str());
        return 1;
    }
    ServerConfig config;
    config.seed = std::strtoll(argv[3], nullptr, 10);
    config.structurePack = argv[2];
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend || !backend->Structures()) {
        std::fprintf(stderr, "no structure generator: %s\n", error.c_str());
        return 1;
    }
    structure::StructureGenerator& gen = *backend->Structures();
    std::ifstream in(argv[4]);
    std::string line;
    std::map<std::string, std::pair<int, int>> score; // type -> (same chunk, asked)
    int same = 0, asked = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string type, rx, rz;
        int x = 0, z = 0;
        ls >> type >> x >> z >> rx;
        if (rx == "none" || rx.empty())
            continue; // the reference found none nearby: nothing to compare
        ls >> rz;
        const int refX = std::atoi(rx.c_str()), refZ = std::atoi(rz.c_str());
        int ox = 0, oz = 0;
        // Searched as far as the reference's answer and a little more: a nearer start of ours
        // shows as a wrong answer, a missing one as none.
        const int radius = static_cast<int>(std::hypot(refX - x, refZ - z) / 16.0) + 4;
        const bool found = gen.Locate(type, x, z, radius, ox, oz);
        const bool match = found && (ox >> 4) == (refX >> 4) && (oz >> 4) == (refZ >> 4);
        // Jigsaw starts are reported at their start piece, which can sit a chunk off the start chunk.
        const bool near = found && std::abs((ox >> 4) - (refX >> 4)) <= 1 && std::abs((oz >> 4) - (refZ >> 4)) <= 1;
        auto& s = score[type];
        s.second++;
        asked++;
        if (near) {
            s.first++;
            same++;
        } else if (std::getenv("LOCATE_VERBOSE")) {
            std::printf("  %s from %d %d: reference %d %d, ours %s %d %d\n", type.c_str(), x, z, refX, refZ,
                        found ? "" : "(none)", ox, oz);
        }
        (void)match;
    }
    for (const auto& [type, s] : score)
        std::printf("%-18s %3d / %3d at the reference's start\n", type.c_str(), s.first, s.second);
    std::printf("total %d / %d (%.1f%%)\n", same, asked, asked ? 100.0 * same / asked : 0.0);
    return 0;
}
