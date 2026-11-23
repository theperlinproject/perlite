#include "network/Batch.hpp"
#include "server/Config.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/levelgen/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include "world/level/block/Blocks.hpp"

using namespace perlite;

int main(int argc, char** argv) {
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 12345;
    const int radius = argc > 2 ? std::atoi(argv[2]) : 3;
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::vector<ChunkPos> pos;
    for (int x = -radius; x < radius; x++)
        for (int z = -radius; z < radius; z++)
            pos.push_back({x, z});
    std::vector<ChunkColumn> cols(pos.size());
    const auto start = std::chrono::steady_clock::now();
    backend->Generate(pos, cols);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::vector<ChunkColumn> decoratedCols;
    double decorateMs = 0;
    int decorated = 0;
    if (backend->Decorates()) {
        const int side = radius * 2;
        const auto at = [&](int x, int z) -> ChunkColumn& { return cols[static_cast<size_t>((x + radius) * side + (z + radius))]; };
        const auto d0 = std::chrono::steady_clock::now();
        for (int x = -radius + 1; x < radius - 1; x++)
            for (int z = -radius + 1; z < radius - 1; z++) {
                const ChunkColumn* region[kDecorationArea];
                for (int dx = -kDecorationReach; dx <= kDecorationReach; dx++)
                    for (int dz = -kDecorationReach; dz <= kDecorationReach; dz++) {
                        const int nx = x + dx, nz = z + dz;
                        region[DecorationSlot(dx, dz)] =
                            nx >= -radius && nx < radius && nz >= -radius && nz < radius ? &at(nx, nz) : nullptr;
                    }
                ChunkColumn out = at(x, z).Clone();
                backend->Decorate({x, z}, region, out);
                decoratedCols.push_back(std::move(out));
                decorated++;
            }
        decorateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - d0).count();
    }
    uint64_t decoratedHash = 1469598103934665603ull;
    for (const ChunkColumn& col : decoratedCols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -64; y < 320; y++)
                    decoratedHash = (decoratedHash ^ col.Get(x, y, z)) * 1099511628211ull;
    std::printf("decorated %d chunks in %.0f ms (%.1f ms each) hash %016llx\n", decorated, decorateMs,
                decorated ? decorateMs / decorated : 0.0, static_cast<unsigned long long>(decoratedHash));
    const BlockCatalog& c = BlockCatalog::Instance();
    const uint16_t water = c.DefaultBlock("water"), lava = c.DefaultBlock("lava");
    long air = 0, wet = 0, hot = 0;
    uint64_t hash = 1469598103934665603ull;
    for (const ChunkColumn& col : cols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -64; y < 320; y++)
                    hash = (hash ^ col.Get(x, y, z)) * 1099511628211ull;
    for (const ChunkColumn& col : cols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -50; y < 30; y++) {
                    const uint16_t b = col.Get(x, y, z);
                    air += b == 0;
                    wet += b == water;
                    hot += b == lava;
                }
    std::printf("%zu chunks in %.0f ms (%.1f ms each); y -50..29: air %ld water %ld lava %ld hash %016llx\n", pos.size(), ms,
                ms / static_cast<double>(pos.size()), air, wet, hot, static_cast<unsigned long long>(hash));
    {
        // Network encoding of the generated columns, as sent to players.
        std::vector<uint8_t> payload;
        uint64_t encodedHash = 1469598103934665603ull;
        const auto e0 = std::chrono::steady_clock::now();
        for (int rep = 0; rep < 5; rep++)
            for (const ChunkColumn& col : decoratedCols.empty() ? cols : decoratedCols) {
                payload.clear();
                col.EncodeNetwork(payload);
                if (rep == 0)
                    for (const uint8_t b : payload)
                        encodedHash = (encodedHash ^ b) * 1099511628211ull;
            }
        const double encodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - e0).count();
        const size_t n = (decoratedCols.empty() ? cols : decoratedCols).size() * 5;
        std::printf("encoded %zu chunks in %.0f ms (%.3f ms each) hash %016llx\n", n, encodeMs,
                    encodeMs / static_cast<double>(n), static_cast<unsigned long long>(encodedHash));
        // Batch compression of those payloads, a few chunks per batch as a session sends them.
        BatchCodec codec;
        codec.EnableCompression(256);
        std::vector<uint8_t> batch;
        size_t raw = 0, packed = 0;
        const auto c0 = std::chrono::steady_clock::now();
        int inBatch = 0;
        for (const ChunkColumn& col : decoratedCols.empty() ? cols : decoratedCols) {
            payload.clear();
            col.EncodeNetwork(payload);
            BatchCodec::AppendPacket(batch, 58, payload);
            if (++inBatch == 6) {
                raw += batch.size();
                packed += codec.Seal(batch).size();
                batch.clear();
                inBatch = 0;
            }
        }
        const double cms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
        std::printf("compressed %.2f MB to %.2f MB in %.0f ms (%.1f MB/s)\n", static_cast<double>(raw) / 1e6,
                    static_cast<double>(packed) / 1e6, cms, static_cast<double>(raw) / 1e3 / cms);
    }
    std::map<std::string, int> tops, all;
    for (const ChunkColumn& col : decoratedCols.empty() ? cols : decoratedCols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                bool top = true;
                for (int y = 319; y >= -64; y--) {
                    const uint16_t b = col.Get(x, y, z);
                    if (b == 0)
                        continue;
                    const std::string name = BlockRegistry::Instance().Name(b);
                    if (top)
                        tops[name]++;
                    top = false;
                    all[name]++;
                }
            }
    std::printf("top blocks:");
    for (const auto& [n, c] : tops)
        std::printf(" %s=%d", n.c_str() + 10, c);
    std::printf("\nall blocks:");
    for (const auto& [n, c] : all)
        std::printf(" %s=%d", n.c_str() + 10, c);
    std::printf("\n");
    return 0;
}
