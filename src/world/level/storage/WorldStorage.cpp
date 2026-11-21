#include "world/level/storage/WorldStorage.hpp"

#include "common/Log.hpp"
#include "world/level/block/Blocks.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

namespace perlite {
    namespace fs = std::filesystem;

    namespace {
        constexpr uint8_t kTagData3D = 43;
        constexpr uint8_t kTagVersion = 44;
        constexpr uint8_t kTagSubChunk = 47;
        constexpr uint8_t kTagFinalized = 54;
        constexpr uint8_t kTagLegacyVersion = 118;
        constexpr int kSubChunkFormat = 9;

        void PutLe32(std::string& out, uint32_t v) {
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<char>(v >> (8 * i)));
        }

        uint32_t GetLe32(std::string_view d, size_t at) {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++)
                v |= static_cast<uint32_t>(static_cast<uint8_t>(d[at + static_cast<size_t>(i)])) << (8 * i);
            return v;
        }

        int BitsFor(size_t paletteSize) {
            if (paletteSize <= 1)
                return 0;
            for (int bits : {1, 2, 3, 4, 5, 6, 8, 16})
                if ((size_t{1} << bits) >= paletteSize)
                    return bits;
            return 16;
        }

        void WriteIndices(std::string& out, const std::array<uint16_t, kCells>& indices, int bits) {
            out.push_back(static_cast<char>(bits << 1));
            if (bits == 0)
                return;
            const int perWord = 32 / bits;
            const int words = (kCells + perWord - 1) / perWord;
            for (int w = 0; w < words; w++) {
                uint32_t word = 0;
                for (int i = 0; i < perWord; i++) {
                    const int cell = w * perWord + i;
                    if (cell >= kCells)
                        break;
                    word |= static_cast<uint32_t>(indices[static_cast<size_t>(cell)]) << (i * bits);
                }
                PutLe32(out, word);
            }
        }

        bool ReadIndices(std::string_view d, size_t& at, std::array<uint16_t, kCells>& indices, int& bits) {
            if (at >= d.size())
                return false;
            bits = static_cast<uint8_t>(d[at++]) >> 1;
            if (bits == 0) {
                indices.fill(0);
                return true;
            }
            if (bits > 16)
                return false;
            const int perWord = 32 / bits;
            const int words = (kCells + perWord - 1) / perWord;
            if (at + static_cast<size_t>(words) * 4 > d.size())
                return false;
            const uint32_t mask = (1u << bits) - 1;
            for (int w = 0; w < words; w++) {
                const uint32_t word = GetLe32(d, at + static_cast<size_t>(w) * 4);
                for (int i = 0; i < perWord; i++) {
                    const int cell = w * perWord + i;
                    if (cell >= kCells)
                        break;
                    indices[static_cast<size_t>(cell)] = static_cast<uint16_t>((word >> (i * bits)) & mask);
                }
            }
            at += static_cast<size_t>(words) * 4;
            return true;
        }

        nbt::Value PaletteEntry(uint16_t id) {
            const BlockRegistry& registry = BlockRegistry::Instance();
            nbt::Value entry = nbt::Value::Make(nbt::Compound);
            entry.SetString("name", registry.Name(id));
            nbt::Value states = nbt::Value::Make(nbt::Compound);
            for (const auto& [key, value] : registry.States(id)) {
                if (const bool* b = std::get_if<bool>(&value))
                    states.SetByte(key, *b ? 1 : 0);
                else if (const int32_t* i = std::get_if<int32_t>(&value))
                    states.SetInt(key, *i);
                else
                    states.SetString(key, std::get<std::string>(value));
            }
            entry.Set("states", std::move(states));
            entry.SetInt("version", WorldStorage::kBlockStateVersion);
            return entry;
        }

        uint16_t BlockFromPalette(const nbt::Value& entry) {
            std::string name = entry.String("name");
            if (name.empty())
                return BlockRegistry::kAir;
            if (!name.contains(':'))
                name = "minecraft:" + name;
            StateList states;
            if (const nbt::Value* s = entry.Find("states"))
                for (const auto& [key, v] : s->compound) {
                    if (v.type == nbt::Byte)
                        states.emplace_back(key, v.integer != 0);
                    else if (v.type == nbt::Int || v.type == nbt::Short || v.type == nbt::Long)
                        states.emplace_back(key, static_cast<int32_t>(v.integer));
                    else if (v.type == nbt::String)
                        states.emplace_back(key, v.text);
                }
            std::sort(states.begin(), states.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            return BlockRegistry::Instance().Intern(name, std::move(states));
        }

        std::string Bytes(const std::vector<uint8_t>& v) {
            return std::string(reinterpret_cast<const char*>(v.data()), v.size());
        }
    }

    std::string WorldStorage::ChunkKey(int dimension, ChunkPos pos, uint8_t tag) {
        std::string key;
        PutLe32(key, static_cast<uint32_t>(pos.x));
        PutLe32(key, static_cast<uint32_t>(pos.z));
        if (dimension != kOverworld)
            PutLe32(key, static_cast<uint32_t>(dimension));
        key.push_back(static_cast<char>(tag));
        return key;
    }

    bool WorldStorage::Open(const std::string& worldDir, std::string& error) {
        dir = worldDir;
        std::error_code ec;
        fs::create_directories(fs::path(worldDir) / "db", ec);
        if (ec) {
            error = "cannot create " + worldDir + ": " + ec.message();
            return false;
        }
        return db.Open((fs::path(worldDir) / "db").string(), error);
    }

    void WorldStorage::Close() {
        db.Close();
    }

    bool WorldStorage::Flush(std::string* error) {
        return db.Commit(error);
    }

    void WorldStorage::EncodeSubChunk(const ChunkColumn& column, int section, int yIndex, std::string& out) {
        out.clear();
        out.push_back(static_cast<char>(kSubChunkFormat));
        out.push_back(1);
        out.push_back(static_cast<char>(static_cast<int8_t>(yIndex)));
        std::vector<uint16_t> palette;
        std::unordered_map<uint16_t, uint16_t> index;
        std::array<uint16_t, kCells> indices{};
        const ChunkColumn::Section& s = column.SectionAt(section);
        if (!s.cells) {
            palette.push_back(s.uniform);
        } else {
            for (int i = 0; i < kCells; i++) {
                const uint16_t id = (*s.cells)[static_cast<size_t>(i)];
                auto [it, inserted] = index.emplace(id, static_cast<uint16_t>(palette.size()));
                if (inserted)
                    palette.push_back(id);
                indices[static_cast<size_t>(i)] = it->second;
            }
        }
        WriteIndices(out, indices, BitsFor(palette.size()));
        PutLe32(out, static_cast<uint32_t>(palette.size()));
        std::vector<uint8_t> nbtBytes;
        for (uint16_t id : palette) {
            nbtBytes.clear();
            nbt::Write(PaletteEntry(id), nbt::Encoding::LittleEndian, nbtBytes);
            out.append(reinterpret_cast<const char*>(nbtBytes.data()), nbtBytes.size());
        }
    }

    bool WorldStorage::DecodeSubChunk(std::string_view d, uint16_t* cells, uint16_t& uniform,
                                      bool& isUniform) {
        if (d.empty())
            return false;
        size_t at = 0;
        const uint8_t format = static_cast<uint8_t>(d[at++]);
        int layers = 1;
        if (format == 8 || format == 9) {
            if (at >= d.size())
                return false;
            layers = static_cast<uint8_t>(d[at++]);
            if (format == 9)
                at++;
        } else if (format != 1) {
            return false;
        }
        if (layers < 1)
            return false;
        std::array<uint16_t, kCells> indices{};
        int bits = 0;
        if (!ReadIndices(d, at, indices, bits))
            return false;
        if (at + 4 > d.size())
            return false;
        const uint32_t count = GetLe32(d, at);
        at += 4;
        if (count == 0 || count > kCells)
            return false;
        std::vector<uint16_t> palette;
        palette.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            nbt::Value entry;
            size_t used = 0;
            const std::span<const uint8_t> rest(reinterpret_cast<const uint8_t*>(d.data()) + at,
                                                d.size() - at);
            if (!nbt::Read(rest, nbt::Encoding::LittleEndian, entry, nullptr, &used))
                return false;
            at += used;
            palette.push_back(BlockFromPalette(entry));
        }
        if (palette.size() == 1) {
            isUniform = true;
            uniform = palette[0];
            return true;
        }
        isUniform = false;
        for (int i = 0; i < kCells; i++) {
            const uint16_t idx = indices[static_cast<size_t>(i)];
            cells[i] = idx < palette.size() ? palette[idx] : BlockRegistry::kAir;
        }
        return true;
    }

    bool WorldStorage::LoadChunk(int dimension, ChunkPos pos, ChunkColumn& out) {
        if (!db.IsOpen())
            return false;
        std::string value;
        const bool hasVersion = db.Get(ChunkKey(dimension, pos, kTagVersion), value) ||
                                db.Get(ChunkKey(dimension, pos, kTagLegacyVersion), value);
        if (!hasVersion)
            return false;
        const DimensionRange range = RangeOf(dimension);
        std::vector<uint16_t> cells(kCells);
        for (int s = 0; s < range.SectionCount(); s++) {
            const int section = range.FirstSection() + s;
            const int yIndex = (range.minY >> 4) + s;
            std::string key = ChunkKey(dimension, pos, kTagSubChunk);
            key.push_back(static_cast<char>(static_cast<int8_t>(yIndex)));
            if (!db.Get(key, value)) {
                out.FillSection(section, BlockRegistry::kAir);
                continue;
            }
            uint16_t uniform = 0;
            bool isUniform = false;
            if (!DecodeSubChunk(value, cells.data(), uniform, isUniform)) {
                out.FillSection(section, BlockRegistry::kAir);
                continue;
            }
            if (isUniform)
                out.FillSection(section, uniform);
            else
                out.SetSection(section, cells.data());
        }
        if (db.Get(ChunkKey(dimension, pos, kTagData3D), value) && value.size() > 512) {
            size_t at = 512;
            std::array<uint16_t, kCells> indices{};
            int bits = 0;
            const int wantSub = (64 - range.minY) >> 4;
            for (int sub = 0; sub <= wantSub && at < value.size(); sub++) {
                if (static_cast<uint8_t>(value[at]) == 0xFF) {
                    at++;
                    continue;
                }
                if (!ReadIndices(value, at, indices, bits) || at + 4 > value.size())
                    break;
                const uint32_t n = bits == 0 ? 1 : GetLe32(value, at);
                if (bits != 0)
                    at += 4;
                std::vector<uint32_t> ids;
                for (uint32_t i = 0; i < n && at + 4 <= value.size(); i++, at += 4)
                    ids.push_back(GetLe32(value, at));
                if (ids.empty())
                    break;
                if (sub == wantSub)
                    for (int x = 0; x < 16; x++)
                        for (int z = 0; z < 16; z++) {
                            const uint16_t idx = indices[static_cast<size_t>(CellIndex(x, 0, z))];
                            const uint32_t id = idx < ids.size() ? ids[idx] : ids[0];
                            out.SetBiome(x, z, out.GenBiome(x, z),
                                         static_cast<uint8_t>(std::min<uint32_t>(id, 255)));
                        }
            }
        }
        return true;
    }

    void WorldStorage::SaveChunk(int dimension, ChunkPos pos, const ChunkColumn& column) {
        if (!db.IsOpen())
            return;
        db.Put(ChunkKey(dimension, pos, kTagVersion), std::string(1, static_cast<char>(kChunkVersion)));
        const DimensionRange range = RangeOf(dimension);
        std::string encoded;
        for (int s = 0; s < range.SectionCount(); s++) {
            const int section = range.FirstSection() + s;
            const int yIndex = (range.minY >> 4) + s;
            std::string key = ChunkKey(dimension, pos, kTagSubChunk);
            key.push_back(static_cast<char>(static_cast<int8_t>(yIndex)));
            if (column.IsUniform(section) && column.SectionAt(section).uniform == BlockRegistry::kAir) {
                db.Delete(key);
                continue;
            }
            EncodeSubChunk(column, section, yIndex, encoded);
            db.Put(key, encoded);
        }
        std::string data3d;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                int top = range.minY;
                for (int y = range.MaxY(); y >= range.minY; y--)
                    if (column.Get(x, y, z) != BlockRegistry::kAir) {
                        top = y + 1;
                        break;
                    }
                const uint16_t h = static_cast<uint16_t>(top - range.minY);
                data3d.push_back(static_cast<char>(h));
                data3d.push_back(static_cast<char>(h >> 8));
            }
        std::vector<uint32_t> biomePalette;
        std::array<uint16_t, kCells> indices{};
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const uint32_t id = column.NetBiome(x, z);
                auto it = std::find(biomePalette.begin(), biomePalette.end(), id);
                if (it == biomePalette.end()) {
                    biomePalette.push_back(id);
                    it = biomePalette.end() - 1;
                }
                const uint16_t idx = static_cast<uint16_t>(it - biomePalette.begin());
                for (int y = 0; y < 16; y++)
                    indices[static_cast<size_t>(CellIndex(x, y, z))] = idx;
            }
        std::string biomeStorage;
        const int bits = BitsFor(biomePalette.size());
        WriteIndices(biomeStorage, indices, bits);
        if (bits != 0)
            PutLe32(biomeStorage, static_cast<uint32_t>(biomePalette.size()));
        for (uint32_t id : biomePalette)
            PutLe32(biomeStorage, id);
        for (int s = 0; s < kSections; s++)
            data3d += s == 0 ? biomeStorage : std::string(1, static_cast<char>(0xFF));
        db.Put(ChunkKey(dimension, pos, kTagData3D), data3d);
        std::string finalized;
        PutLe32(finalized, 2);
        db.Put(ChunkKey(dimension, pos, kTagFinalized), finalized);
    }

    std::vector<uint8_t> WorldStorage::LevelDat(const LevelInfo& info) {
        nbt::Value root = nbt::Value::Make(nbt::Compound);
        root.SetString("LevelName", info.name);
        root.SetLong("RandomSeed", static_cast<int64_t>(info.seed));
        root.SetInt("SpawnX", info.spawnX);
        root.SetInt("SpawnY", info.spawnY);
        root.SetInt("SpawnZ", info.spawnZ);
        root.SetLong("Time", info.time);
        root.SetLong("currentTick", info.currentTick);
        root.SetInt("GameType", info.gameType);
        root.SetInt("Difficulty", info.difficulty);
        root.SetInt("Generator", info.generator);
        root.SetInt("StorageVersion", 10);
        root.SetInt("NetworkVersion", 2225);
        root.SetInt("Platform", 2);
        root.SetLong("LastPlayed",
                     static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count()));
        root.SetFloat("rainLevel", info.raining ? 1.0f : 0.0f);
        root.SetInt("rainTime", info.raining ? 6000 : 0);
        root.SetFloat("lightningLevel", info.thundering ? 1.0f : 0.0f);
        root.SetInt("lightningTime", info.thundering ? 6000 : 0);
        root.SetByte("commandsEnabled", 1);
        root.SetString("BiomeOverride", "");
        root.SetByte("hasBeenLoadedInCreative", info.gameType == 1 ? 1 : 0);
        for (const char* key : {"lastOpenedWithVersion", "MinimumCompatibleClientVersion"}) {
            nbt::Value list = nbt::Value::Make(nbt::List);
            list.listType = nbt::Int;
            for (int v : {1, 21, 60, 0, 0}) {
                nbt::Value n = nbt::Value::Make(nbt::Int);
                n.integer = v;
                list.list.push_back(n);
            }
            root.Set(key, std::move(list));
        }
        for (const auto& [name, value] : info.boolRules)
            root.SetByte(name, value ? 1 : 0);
        for (const auto& [name, value] : info.intRules)
            root.SetInt(name, value);
        std::vector<uint8_t> body;
        nbt::Write(root, nbt::Encoding::LittleEndian, body);
        std::vector<uint8_t> out;
        const uint32_t header[2] = {10, static_cast<uint32_t>(body.size())};
        for (uint32_t h : header)
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(h >> (8 * i)));
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    bool WorldStorage::ParseLevelDat(const std::vector<uint8_t>& bytes, LevelInfo& info) {
        if (bytes.size() < 8)
            return false;
        nbt::Value root;
        if (!nbt::Read(std::span<const uint8_t>(bytes.data() + 8, bytes.size() - 8),
                       nbt::Encoding::LittleEndian, root))
            return false;
        info.name = root.String("LevelName", info.name);
        info.seed = static_cast<uint64_t>(root.Int("RandomSeed", static_cast<int64_t>(info.seed)));
        info.spawnX = static_cast<int>(root.Int("SpawnX", info.spawnX));
        info.spawnY = static_cast<int>(root.Int("SpawnY", info.spawnY));
        info.spawnZ = static_cast<int>(root.Int("SpawnZ", info.spawnZ));
        info.time = root.Int("Time", info.time);
        info.currentTick = root.Int("currentTick", info.currentTick);
        info.gameType = static_cast<int>(root.Int("GameType", info.gameType));
        info.difficulty = static_cast<int>(root.Int("Difficulty", info.difficulty));
        info.generator = static_cast<int>(root.Int("Generator", info.generator));
        info.raining = root.Real("rainLevel", 0) > 0.0;
        info.thundering = root.Real("lightningLevel", 0) > 0.0;
        for (const auto& [key, v] : root.compound) {
            if (key.empty() || !std::islower(static_cast<unsigned char>(key[0])))
                continue;
            if (key == "currentTick" || key == "commandsEnabled" || key == "lastOpenedWithVersion" ||
                key == "rainLevel" || key == "rainTime" || key == "lightningLevel" ||
                key == "lightningTime" || key == "hasBeenLoadedInCreative")
                continue;
            if (v.type == nbt::Byte)
                info.boolRules.emplace_back(key, v.integer != 0);
            else if (v.type == nbt::Int)
                info.intRules.emplace_back(key, static_cast<int>(v.integer));
        }
        return true;
    }

    bool WorldStorage::LoadLevel(LevelInfo& info) const {
        std::ifstream in(fs::path(dir) / "level.dat", std::ios::binary);
        if (!in)
            return false;
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
        return ParseLevelDat(bytes, info);
    }

    bool WorldStorage::SaveLevel(const LevelInfo& info) const {
        if (dir.empty())
            return false;
        const std::vector<uint8_t> bytes = LevelDat(info);
        const fs::path target = fs::path(dir) / "level.dat";
        const fs::path temp = fs::path(dir) / "level.dat.tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out)
                return false;
        }
        std::error_code ec;
        if (fs::exists(target, ec))
            fs::copy_file(target, fs::path(dir) / "level.dat_old", fs::copy_options::overwrite_existing, ec);
        fs::rename(temp, target, ec);
        if (ec)
            return false;
        std::ofstream(fs::path(dir) / "levelname.txt", std::ios::trunc) << info.name;
        return true;
    }

    bool WorldStorage::LoadPlayer(const std::string& id, nbt::Value& data) const {
        std::string value;
        if (!db.Get("player_server_" + id, value))
            return false;
        return nbt::Read(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data()), value.size()),
            nbt::Encoding::LittleEndian, data);
    }

    void WorldStorage::SavePlayer(const std::string& id, const nbt::Value& data) {
        std::vector<uint8_t> bytes;
        nbt::Write(data, nbt::Encoding::LittleEndian, bytes);
        db.Put("player_server_" + id, Bytes(bytes));
    }

    std::vector<nbt::Value> WorldStorage::LoadBlockEntities(int dimension, ChunkPos pos) const {
        std::vector<nbt::Value> out;
        std::string value;
        if (!db.Get(ChunkKey(dimension, pos, 49), value))
            return out;
        size_t at = 0;
        while (at < value.size()) {
            nbt::Value entity;
            size_t used = 0;
            const std::span<const uint8_t> rest(reinterpret_cast<const uint8_t*>(value.data()) + at,
                                                value.size() - at);
            if (!nbt::Read(rest, nbt::Encoding::LittleEndian, entity, nullptr, &used) || used == 0)
                break;
            at += used;
            out.push_back(std::move(entity));
        }
        return out;
    }

    void WorldStorage::SaveBlockEntities(int dimension, ChunkPos pos,
                                         const std::vector<nbt::Value>& entities) {
        const std::string key = ChunkKey(dimension, pos, 49);
        if (entities.empty()) {
            db.Delete(key);
            return;
        }
        std::vector<uint8_t> bytes;
        for (const nbt::Value& e : entities)
            nbt::Write(e, nbt::Encoding::LittleEndian, bytes);
        db.Put(key, Bytes(bytes));
    }
}
