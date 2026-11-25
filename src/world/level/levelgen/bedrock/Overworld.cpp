#include "world/level/levelgen/bedrock/Overworld.hpp"

#include "server/VanillaPack.hpp"
#include "world/level/levelgen/bedrock/Spawn.hpp"
#include "world/level/levelgen/vanilla/BiomeData.hpp"

#include <cstdlib>
#include <cstring>

namespace perlite::bedrock {

    std::string PackFolder(const ServerConfig& config) {
        if (const char* env = std::getenv("PERLITE_VANILLA_PACK"); env && *env)
            return env;
        return vanillapack::Ensure(config.vanillaPackDir.empty() ? config.dataDir : config.vanillaPackDir, true);
    }

    struct OverworldBackend::Caches {
        struct Shard {
            std::mutex mutex;
            std::unordered_map<uint64_t, int16_t> surface;
            std::unordered_map<uint64_t, std::shared_ptr<const CornerColumns>> corners;
        };
        std::array<Shard, 16> shards;
        static uint64_t Key(int a, int b) { return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b); }
        Shard& For(uint64_t key) { return shards[(key * 0x9E3779B97F4A7C15ull) >> 60]; }
    };

    OverworldBackend::~OverworldBackend() = default;

    std::unique_ptr<OverworldBackend> OverworldBackend::Create(const ServerConfig& config, std::string& error) {
        const std::string folder = PackFolder(config);
        if (folder.empty()) {
            error = "the Bedrock generator needs the vanilla behavior pack, and it could not be installed";
            return nullptr;
        }
        auto pack = Pack::Load(folder, error);
        if (!pack)
            return nullptr;
        std::unique_ptr<OverworldBackend> b(new OverworldBackend());
        b->seed = static_cast<int64_t>(config.seed);
        b->pack = pack;
        Xoroshiro random(b->seed);
        const PositionalFactory factory = random.ForkPositional();
        OverworldSettings settings = OverworldSettings::For(Preset::Default);
        b->terrain = std::make_unique<BedrockTerrain>(factory, settings, DimensionDefinition{});
        b->buffers = std::make_unique<BufferNoises>(factory);
        b->fill = std::make_unique<TerrainFill>(factory, GenBlocks::Get());
        b->surface = std::make_unique<SurfacePass>(b->seed, factory, *pack);
        b->carvers = std::make_unique<Carvers>(static_cast<uint32_t>(LevelSeed32(b->seed)));
        b->caches = std::make_unique<Caches>();
        (void)BiomeTree::Overworld();
        return b;
    }

    int OverworldBackend::PreliminarySurface(int qx, int qz) const {
        const uint64_t key = Caches::Key(qx, qz);
        auto& shard = caches->For(key);
        {
            std::lock_guard lock(shard.mutex);
            if (const auto it = shard.surface.find(key); it != shard.surface.end())
                return it->second;
        }
        const int v = terrain->PreliminarySurface(qx, qz);
        std::lock_guard lock(shard.mutex);
        if (shard.surface.size() > 1 << 16)
            shard.surface.clear();
        shard.surface.emplace(key, static_cast<int16_t>(v));
        return v;
    }

    std::shared_ptr<const CornerColumns> OverworldBackend::Corners(int cx, int cz) const {
        const uint64_t key = Caches::Key(cx, cz);
        auto& shard = caches->For(key);
        {
            std::lock_guard lock(shard.mutex);
            if (const auto it = shard.corners.find(key); it != shard.corners.end())
                return it->second;
        }
        auto made = std::make_shared<const CornerColumns>(MakeCornerColumns(*terrain, *buffers, cx, cz));
        std::lock_guard lock(shard.mutex);
        if (shard.corners.size() > 256)
            shard.corners.clear();
        return shard.corners.emplace(key, std::move(made)).first->second;
    }

    std::vector<int> OverworldBackend::BiomeSamples(int cx, int cz) const {
        const DimensionDefinition def = terrain->Definition();
        const int levels = SampleLevels(def.height);
        const int originY = def.minY - kSampleSpacing;
        std::vector<int> samples(static_cast<size_t>(kSampleColumns * kSampleColumns * levels));
        const BiomeTree& tree = BiomeTree::Overworld();
        for (int xi = 0; xi < kSampleColumns; xi++)
            for (int zi = 0; zi < kSampleColumns; zi++) {
                const int x = (cx << 4) - 8 + xi * 8, z = (cz << 4) - 8 + zi * 8;
                const ClimateColumn column = terrain->Column(x, z);
                BiomeTree::Nearest previous;
                bool have = false;
                for (int yi = 0; yi < levels; yi++) {
                    const auto n = tree.Find(column.Point(originY + yi * kSampleSpacing), have ? &previous : nullptr);
                    samples[static_cast<size_t>(SampleIndex(xi, zi, yi, levels))] = tree.BiomeId(n);
                    previous = n;
                    have = true;
                }
            }
        return samples;
    }

    int OverworldBackend::BiomeAt(int x, int y, int z) const {
        const BiomeTree& tree = BiomeTree::Overworld();
        return tree.BiomeId(tree.Find(terrain->Column(x, z).Point(y), nullptr));
    }

    ProtoChunk OverworldBackend::Build(int cx, int cz, BiomeVolume* biomesOut) const {
        const DimensionDefinition def = terrain->Definition();
        ProtoChunk chunk(cx, cz, def.minY, def.height);
        const auto own = Corners(cx, cz), east = Corners(cx + 1, cz), south = Corners(cx, cz + 1), se = Corners(cx + 1, cz + 1);
        const NoiseChunk noise = NoiseChunk::Assemble(*own, *east, *south, *se);
        const SurfaceAt surfaceAt = [this](int qx, int qz) { return PreliminarySurface(qx, qz); };
        fill->Run(*terrain, noise, chunk, surfaceAt);
        const BiomeVolume volume = BiomeVolume::Resolve(BiomeSamples(cx, cz), LevelSeed32(seed), cx, cz, def.minY, def.height);
        surface->Build(*terrain, chunk, volume);
        carvers->Carve(fill->Aquifer(), *terrain, surfaceAt, chunk);
        if (biomesOut)
            *biomesOut = volume;
        return chunk;
    }

    namespace {
        uint8_t TableIndexOf(int biomeId) {
            static const std::vector<int> map = [] {
                std::vector<int> m(256, 0);
                for (int i = 0; i < kBiomeTableSize; i++) {
                    if (kBiomeTable[i].id < 0 || kBiomeTable[i].id > 255)
                        continue;
                    for (int j = 0; j < vanilla::kBiomeCount; j++)
                        if (std::strcmp(vanilla::kBiomes[j].bedrock, kBiomeTable[i].pack) == 0) {
                            m[static_cast<size_t>(kBiomeTable[i].id)] = j;
                            break;
                        }
                }
                return m;
            }();
            return static_cast<uint8_t>(map[static_cast<size_t>(biomeId & 255)]);
        }
    }

    void OverworldBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        for (size_t i = 0; i < positions.size(); i++) {
            BiomeVolume volume;
            const ProtoChunk chunk = Build(positions[i].x, positions[i].z, &volume);
            ChunkColumn& col = out[i];
            std::array<uint16_t, kCells> cells{};
            for (int s = 0; s < chunk.height / 16; s++) {
                bool uniform = true;
                const uint16_t first = chunk.blocks[static_cast<size_t>(s * 16) * 256];
                for (int ly = 0; ly < 16; ly++)
                    for (int lz = 0; lz < 16; lz++)
                        for (int lx = 0; lx < 16; lx++) {
                            const uint16_t b = chunk.blocks[static_cast<size_t>(s * 16 + ly) * 256 + static_cast<size_t>((lz << 4) | lx)];
                            cells[static_cast<size_t>(CellIndex(lx, ly, lz))] = b;
                            uniform &= b == first;
                        }
                const int section = s + ((chunk.minY - kMinY) >> 4);
                if (uniform)
                    col.FillSection(section, first);
                else
                    col.SetSection(section, cells.data());
            }
            constexpr int layers = ChunkColumn::kQuartLayers;
            std::vector<uint8_t> table(static_cast<size_t>(16 * layers)), net(static_cast<size_t>(16 * layers));
            for (int qy = 0; qy < layers; qy++)
                for (int qz = 0; qz < 4; qz++)
                    for (int qx = 0; qx < 4; qx++) {
                        const int id = volume.IdAt(qx * 4 + 2, kMinY + qy * 4 + 2, qz * 4 + 2);
                        const size_t index = static_cast<size_t>(ChunkColumn::QuartIndex(qx, qy, qz));
                        table[index] = TableIndexOf(id);
                        net[index] = static_cast<uint8_t>(id);
                    }
            col.SetQuartBiomes(table.data(), net.data());
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    const int top = std::max<int>(chunk.tops[static_cast<size_t>((z << 4) | x)], kSeaLevel);
                    const int id = volume.IdAt(x, top, z);
                    col.SetBiome(x, z, TableIndexOf(id), static_cast<uint8_t>(id));
                }
            col.generatedNow = true;
        }
    }

    bool OverworldBackend::FindSpawn(int& x, int& y, int& z) {
        const auto [sx, sz] = SpawnColumn(*terrain);
        const ProtoChunk chunk = Build(sx >> 4, sz >> 4);
        const int lx = sx & 15, lz = sz & 15;
        int top = chunk.minY;
        for (int yy = chunk.minY + chunk.height - 1; yy >= chunk.minY; yy--)
            if (chunk.Get(lx, yy, lz) != 0) {
                top = yy;
                break;
            }
        x = sx;
        z = sz;
        y = std::max(top, chunk.minY) + 1;
        return true;
    }
}
