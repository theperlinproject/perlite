// What blocks need under or beside them to stay: plants on soil, sea grass on a sturdy floor, vines on a
// full face.
#include "test_util.hpp"

#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/bedrock/BlockTraits.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <tuple>

using namespace perlite;
using namespace perlite::bedrock;

namespace {
    struct MapView : BlockView {
        std::map<std::tuple<int, int, int>, BlockId> blocks;
        BlockId Get(const IVec3& p) const override {
            const auto it = blocks.find({p.x, p.y, p.z});
            return it == blocks.end() ? kAirId : it->second;
        }
        void Put(const IVec3& p, BlockId b) { blocks[{p.x, p.y, p.z}] = b; }
    };
}

int main(int argc, char** argv) {
    std::string error;
    const std::string dir = argc > 1 ? argv[1] : "data";
    if (!BlockCatalog::Instance().Load(BlockCatalog::FindFile(dir, "block_states.json"), BlockCatalog::FindFile(dir, "block_destroy.tsv"), error)) {
        std::printf("block data: %s\n", error.c_str());
        return 1;
    }
    const Blocks& b = Blocks::Get();
    const BlockId grass = b.Id("minecraft:short_grass"), dirt = b.Id("minecraft:dirt"), stone = b.Id("minecraft:stone");
    CHECK(grass != kAirId && dirt != kAirId);
    MapView view;
    const IVec3 at{0, 64, 0};
    view.Put(at, grass);
    view.Put(at.Down(), dirt);
    CHECK(CanSurvive(view, grass, at));
    view.Put(at.Down(), stone);
    CHECK(!CanSurvive(view, grass, at));
    view.Put(at.Down(), kAirId);
    CHECK(!CanSurvive(view, grass, at));
    CHECK(b.Entry(stone).full && b.Entry(stone).solid);
    CHECK(!b.Entry(b.Id("minecraft:air")).solid || b.IsAir(b.Id("minecraft:air")));
    CHECK(b.Is(b.Id("minecraft:oak_leaves"), Tag::Leaves));

    // A vine hanging on the south face keeps it only while that block is there.
    const BlockId vine = b.IdWith("minecraft:vine", {{"vine_direction_bits", StateValue(int32_t(1))}});
    Shapes shapes;
    view.blocks.clear();
    view.Put(at, vine);
    view.Put(at + IVec3{0, 0, 1}, stone);
    CHECK(shapes.UpdateFromNeighbours(view, at) == vine);
    view.Put(at + IVec3{0, 0, 1}, kAirId);
    CHECK(shapes.UpdateFromNeighbours(view, at) == kAirId);

    if (g_failed)
        return 1;
    std::printf("block_support_test: ok\n");
    return 0;
}
