#include "world/level/chunk/Chunk.hpp"

#include "world/level/block/Blocks.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace perlite {
    namespace {
        void PutVarint(std::vector<uint8_t>& o, uint32_t v) {
            while (v >= 0x80) {
                o.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
                v >>= 7;
            }
            o.push_back(static_cast<uint8_t>(v));
        }

        void PutZigzag(std::vector<uint8_t>& o, int32_t v) {
            PutVarint(o, (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
        }

        unsigned BitsFor(size_t paletteSize) {
            unsigned need = 0;
            while ((1u << need) < paletteSize)
                need++;
            return need == 0   ? 0
                   : need <= 1 ? 1
                   : need <= 2 ? 2
                   : need <= 3 ? 3
                   : need <= 4 ? 4
                   : need <= 5 ? 5
                   : need <= 6 ? 6
                   : need <= 8 ? 8
                               : 16;
        }

        // One paletted storage: header, the packed indices (little-endian words), then the palette.
        void WritePalette(std::vector<uint8_t>& out, const int32_t* palette, size_t paletteSize, const uint16_t* idx) {
            const unsigned bits = BitsFor(paletteSize);
            out.push_back(static_cast<uint8_t>((bits << 1) | 1));
            if (bits == 0) {
                PutZigzag(out, palette[0]);
                return;
            }
            const unsigned perWord = 32 / bits;
            const unsigned words = (kCells + perWord - 1) / perWord;
            const size_t at = out.size();
            out.resize(at + static_cast<size_t>(words) * 4);
            uint8_t* dst = out.data() + at;
            unsigned i = 0;
            for (unsigned w = 0; w < words; w++) {
                uint32_t word = 0;
                for (unsigned k = 0; k < perWord && i < static_cast<unsigned>(kCells); k++, i++)
                    word |= static_cast<uint32_t>(idx[i]) << (k * bits);
                dst[0] = static_cast<uint8_t>(word);
                dst[1] = static_cast<uint8_t>(word >> 8);
                dst[2] = static_cast<uint8_t>(word >> 16);
                dst[3] = static_cast<uint8_t>(word >> 24);
                dst += 4;
            }
            PutZigzag(out, static_cast<int32_t>(paletteSize));
            for (size_t k = 0; k < paletteSize; k++)
                PutZigzag(out, palette[k]);
        }

        // A storage of arbitrary values; the palette is in order of first appearance.
        void WriteStorage(std::vector<uint8_t>& out, const std::array<int32_t, kCells>& values) {
            static thread_local std::vector<int32_t> palette;
            static thread_local std::unordered_map<int32_t, uint32_t> lookup;
            palette.clear();
            lookup.clear();
            std::array<uint16_t, kCells> idx;
            int32_t lastValue = values[0];
            uint32_t lastIndex = 0;
            palette.push_back(lastValue);
            for (int i = 0; i < kCells; i++) {
                const int32_t v = values[static_cast<size_t>(i)];
                if (v != lastValue) {
                    uint32_t found = UINT32_MAX;
                    if (palette.size() <= 24) {
                        for (uint32_t k = 0; k < palette.size(); k++)
                            if (palette[k] == v) {
                                found = k;
                                break;
                            }
                    } else {
                        if (lookup.empty())
                            for (uint32_t k = 0; k < palette.size(); k++)
                                lookup.emplace(palette[k], k);
                        if (const auto it = lookup.find(v); it != lookup.end())
                            found = it->second;
                    }
                    if (found == UINT32_MAX) {
                        found = static_cast<uint32_t>(palette.size());
                        palette.push_back(v);
                        if (!lookup.empty())
                            lookup.emplace(v, found);
                    }
                    lastValue = v;
                    lastIndex = found;
                }
                idx[static_cast<size_t>(i)] = static_cast<uint16_t>(lastIndex);
            }
            WritePalette(out, palette.data(), palette.size(), idx.data());
        }

        // A section of block ids. The palette is built on the ids, and only its entries are turned into
        // network hashes; the bytes are the same as writing the hashes of every cell.
        void WriteBlocks(std::vector<uint8_t>& out, const std::array<uint16_t, kCells>& cells,
                         const BlockRegistry& blocks) {
            static thread_local std::vector<int32_t> slotOf(0x10000, -1);
            static thread_local std::vector<uint16_t> ids;
            static thread_local std::vector<int32_t> palette;
            ids.clear();
            std::array<uint16_t, kCells> idx;
            for (int i = 0; i < kCells; i++) {
                const uint16_t id = cells[static_cast<size_t>(i)];
                int32_t& slot = slotOf[id];
                if (slot < 0) {
                    slot = static_cast<int32_t>(ids.size());
                    ids.push_back(id);
                }
                idx[static_cast<size_t>(i)] = static_cast<uint16_t>(slot);
            }
            palette.resize(ids.size());
            bool distinct = true;
            for (size_t k = 0; k < ids.size(); k++) {
                palette[k] = blocks.Hash(ids[k]);
                slotOf[ids[k]] = -1;
            }
            if (palette.size() > 1) {
                std::vector<int32_t> sorted = palette;
                std::ranges::sort(sorted);
                distinct = std::ranges::adjacent_find(sorted) == sorted.end();
            }
            if (distinct) {
                WritePalette(out, palette.data(), palette.size(), idx.data());
                return;
            }
            // Two ids with one network hash share a palette entry.
            std::array<int32_t, kCells> values;
            for (int i = 0; i < kCells; i++)
                values[static_cast<size_t>(i)] = blocks.Hash(cells[static_cast<size_t>(i)]);
            WriteStorage(out, values);
        }
    }

    uint16_t ChunkColumn::Get(int x, int y, int z) const {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < kMinY || y >= kMinY + kHeight)
            return 0;
        const Section& s = sections[static_cast<size_t>((y - kMinY) >> 4)];
        return s.cells ? (*s.cells)[static_cast<size_t>(CellIndex(x, y - kMinY, z))] : s.uniform;
    }

    void ChunkColumn::Set(int x, int y, int z, uint16_t block) {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < kMinY || y >= kMinY + kHeight)
            return;
        Section& s = sections[static_cast<size_t>((y - kMinY) >> 4)];
        if (!s.cells) {
            if (s.uniform == block)
                return;
            s.cells = std::make_unique<std::array<uint16_t, kCells>>();
            s.cells->fill(s.uniform);
        }
        (*s.cells)[static_cast<size_t>(CellIndex(x, y - kMinY, z))] = block;
        version++;
    }

    ChunkColumn ChunkColumn::Clone() const {
        ChunkColumn c;
        for (size_t i = 0; i < sections.size(); i++) {
            c.sections[i].uniform = sections[i].uniform;
            if (sections[i].cells)
                c.sections[i].cells = std::make_unique<std::array<uint16_t, kCells>>(*sections[i].cells);
        }
        c.genBiomes = genBiomes;
        c.netBiomes = netBiomes;
        c.quartBiomes = quartBiomes;
        c.quartNet = quartNet;
        c.hasQuartBiomes = hasQuartBiomes;
        c.version = version;
        return c;
    }

    void ChunkColumn::FillSection(int section, uint16_t block) {
        Section& s = sections[static_cast<size_t>(section)];
        s.cells.reset();
        s.uniform = block;
    }

    void ChunkColumn::SetSection(int section, const uint16_t* cells) {
        Section& s = sections[static_cast<size_t>(section)];
        if (std::all_of(cells, cells + kCells, [&](uint16_t v) { return v == cells[0]; })) {
            FillSection(section, cells[0]);
            return;
        }
        if (!s.cells)
            s.cells = std::make_unique<std::array<uint16_t, kCells>>();
        std::copy(cells, cells + kCells, s.cells->begin());
    }

    void ChunkColumn::EncodeNetwork(std::vector<uint8_t>& out, int dimension) const {
        const BlockRegistry& blocks = BlockRegistry::Instance();
        const DimensionRange range = RangeOf(dimension);
        const int first = range.FirstSection(), count = range.SectionCount();
        std::array<int32_t, kCells> values;
        out.reserve(out.size() + static_cast<size_t>(count) * 2200 + 2048);
        for (int sec = first; sec < first + count; sec++) {
            const Section& s = sections[static_cast<size_t>(sec)];
            out.push_back(9);
            out.push_back(1);
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>((kMinY >> 4) + sec)));
            if (s.cells) {
                WriteBlocks(out, *s.cells, blocks);
            } else {
                out.push_back(1);  // no bits per cell: the one palette entry follows
                PutZigzag(out, blocks.Hash(s.uniform));
            }
        }
        if (!hasQuartBiomes) {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 16; y++)
                        values[static_cast<size_t>(CellIndex(x, y, z))] = netBiomes[static_cast<size_t>(x * 16 + z)];
            for (int sec = 0; sec < count; sec++)
                WriteStorage(out, values);
        } else {
            for (int sec = first; sec < first + count; sec++) {
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++)
                        for (int y = 0; y < 16; y++)
                            values[static_cast<size_t>(CellIndex(x, y, z))] =
                                quartNet[static_cast<size_t>(QuartIndex(x >> 2, sec * 4 + (y >> 2), z >> 2))];
                WriteStorage(out, values);
            }
        }
        out.push_back(0);
    }
}
