#include "server/Server.hpp"

#include "network/session/Session.hpp"
#include "world/level/block/Waterlog.hpp"
#include "world/level/chunk/ChunkManager.hpp"

#include <algorithm>
#include <tuple>
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/bedrock/BlockTraits.hpp"

#include <string>

namespace perlite {

    namespace {
        using bedrock::BlockEntry;
        using bedrock::Blocks;
        using bedrock::IVec3;

        // The world as the block rules read it: the liquid layer's water shows as a flooded block.
        class WorldView : public bedrock::BlockView {
        public:
            explicit WorldView(ChunkManager& world) : world(world) {}
            bedrock::BlockId Get(const IVec3& p) const override {
                if (p.y < kMinY || p.y >= kMinY + kHeight || !world.Peek({p.x >> 4, p.z >> 4}))
                    return bedrock::kAirId;
                const uint16_t block = world.GetBlock(p.x, p.y, p.z);
                if (block >= bedrock::kFloodedBit)
                    return bedrock::kAirId;
                if (waterlog::IsWater(world.GetLiquid(p.x, p.y, p.z)))
                    return Blocks::Get().Flooded(block);
                return block;
            }

        private:
            ChunkManager& world;
        };

        std::string Bare(const std::string& name) { return name.starts_with("minecraft:") ? name.substr(10) : name; }

        bool Sturdy(const Blocks& b, bedrock::BlockId id) { return b.Entry(id).full; }
        bool TopCentre(const Blocks& b, bedrock::BlockId id) { return b.Entry(id).topCentre; }

        // facing_direction numbers: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
        IVec3 FacingOffset(int facing) {
            static const IVec3 kOffsets[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            return kOffsets[facing < 0 || facing > 5 ? 1 : facing];
        }
    }

    void Server::QueueSupportChecks(int dimension, int x, int y, int z) {
        if (supportQueue.size() >= 65536)
            return;
        static const int kAround[7][3] = {{0, 0, 0}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const auto& d : kAround)
            supportQueue.push_back({dimension, x + d[0], y + d[1], z + d[2]});
    }

    bool Server::StillSupported(int dimension, int x, int y, int z, uint16_t block) {
        const Blocks& b = Blocks::Get();
        if (block == BlockRegistry::kAir || block >= bedrock::kFloodedBit)
            return true;
        ChunkManager& world = World(dimension);
        const WorldView view(world);
        const IVec3 pos{x, y, z};
        const BlockEntry& e = b.Entry(block);
        if (e.liquid)
            return true;
        const std::string bare = Bare(e.name);
        const bedrock::BlockId below = view.Get(pos.Down()), above = view.Get(pos.Up());

        // Two-block plants and doors hold each other up; the halves break together elsewhere.
        if (e.upper == 1)
            return b.SameKind(below, block);

        // Blocks the generator's rules know (plants, corals, snow, dripleaf, moss...).
        if (e.support != bedrock::Support::Anywhere && !bedrock::CanSurvive(view, view.Get(pos), pos))
            return false;
        if (bare == "vine" || bare == "glow_lichen" || bare == "sculk_vein" || bare == "cocoa") {
            bedrock::Shapes shapes;
            shapes.water = waterlog::Water();
            shapes.doublePlantsKeepOrphans = false;
            return shapes.UpdateFromNeighbours(view, pos) == view.Get(pos);
        }

        // Torches lean on the block on the side they name, or stand on the one below.
        if (const std::string* torch = b.Text(block, "torch_facing_direction")) {
            if (*torch == "top" || *torch == "unknown")
                return TopCentre(b, below);
            static const std::pair<const char*, IVec3> kSides[4] = {
                {"north", {0, 0, -1}}, {"south", {0, 0, 1}}, {"west", {-1, 0, 0}}, {"east", {1, 0, 0}}};
            for (const auto& [side, offset] : kSides)
                if (*torch == side)
                    return Sturdy(b, view.Get(pos + offset));
            return true;
        }
        // Ladders, wall signs and banners hang on the block behind them; buttons sit on the face they were
        // pressed against.
        if (const auto facing = b.Int(block, "facing_direction")) {
            if (bare == "ladder" || bare.ends_with("wall_sign") || bare.ends_with("wall_banner"))
                return Sturdy(b, view.Get(pos - FacingOffset(*facing)));
            if (bare.ends_with("button"))
                return TopCentre(b, view.Get(pos - FacingOffset(*facing)));
        }
        // Floor blocks: rails, carpets, plates, redstone, flower pots, candles, signs and banners on the ground.
        const bool crop = bare == "wheat" || bare == "carrots" || bare == "potatoes" || bare == "beetroot" ||
                          bare == "melon_stem" || bare == "pumpkin_stem" || bare == "torchflower_crop" || bare == "pitcher_crop";
        if (crop)
            return b.NameIs(below, "minecraft:farmland");
        if (bare.ends_with("carpet") && bare != "moss_carpet" && bare != "pale_moss_carpet")
            return !b.IsAir(below);
        if (bare.ends_with("rail") || bare.ends_with("pressure_plate") || bare == "redstone_wire" ||
            bare.ends_with("repeater") || bare.ends_with("comparator") || bare == "flower_pot" || bare.ends_with("candle") ||
            bare.ends_with("standing_sign") || bare == "standing_banner" || bare == "frame" || bare == "sapling" ||
            bare.ends_with("_sapling") || bare == "nether_wart" || bare == "sweet_berry_bush")
            return TopCentre(b, below) || (bare == "nether_wart" && b.NameIs(below, "minecraft:soul_sand"));
        if (bare.ends_with("_door") && e.upper == 0)
            return TopCentre(b, below);
        if (bare == "lantern" || bare == "soul_lantern" || bare == "copper_lantern")
            return b.Flag(block, "hanging") ? !b.IsAir(above) : !b.IsAir(below);
        return true;
    }

    void Server::TickSupport() {
        if (supportQueue.empty())
            return;
        std::vector<SupportCheck> due = std::move(supportQueue);
        supportQueue.clear();
        std::sort(due.begin(), due.end(), [](const SupportCheck& a, const SupportCheck& b) {
            return std::tie(a.dimension, a.x, a.y, a.z) < std::tie(b.dimension, b.x, b.y, b.z);
        });
        due.erase(std::unique(due.begin(), due.end(),
                              [](const SupportCheck& a, const SupportCheck& b) {
                                  return a.dimension == b.dimension && a.x == b.x && a.y == b.y && a.z == b.z;
                              }),
                  due.end());
        for (const SupportCheck& c : due) {
            ChunkManager& world = World(c.dimension);
            const DimensionRange range = RangeOf(c.dimension);
            if (c.y < range.minY || c.y > range.MaxY() || !world.Peek({c.x >> 4, c.z >> 4}))
                continue;
            const uint16_t block = world.GetBlock(c.x, c.y, c.z);
            if (StillSupported(c.dimension, c.x, c.y, c.z, block))
                continue;
            if (RuleValue("dotiledrops"))
                DropBlockLoot(c.dimension, c.x, c.y, c.z, block, std::string(), 0.0f, nullptr, 0, 0);
            ApplyBlock(c.dimension, c.x, c.y, c.z, BlockRegistry::kAir);
            const int32_t hash = BlockRegistry::Instance().Hash(block);
            for (Session* s : PlayersIn(c.dimension))
                s->SendLevelEvent(2001, c.x + 0.5, c.y + 0.5, c.z + 0.5, hash);
            BreakPartner(c.dimension, c.x, c.y, c.z, block);
        }
    }
}
