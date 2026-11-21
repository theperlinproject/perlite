#include "server/packs/ResourcePackManager.hpp"

#include "common/Json.hpp"
#include "common/Log.hpp"

#include <miniz.h>
#include <openssl/evp.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace perlite {
    namespace fs = std::filesystem;

    namespace {
        std::vector<uint8_t> ReadFile(const fs::path& p) {
            std::ifstream in(p, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        }

        bool ZipFolder(const fs::path& root, std::vector<uint8_t>& out) {
            mz_zip_archive zip{};
            if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20))
                return false;
            std::vector<fs::path> files;
            std::error_code ec;
            for (const auto& e : fs::recursive_directory_iterator(root, ec))
                if (e.is_regular_file(ec))
                    files.push_back(e.path());
            std::sort(files.begin(), files.end());
            bool ok = true;
            for (const fs::path& f : files) {
                const std::vector<uint8_t> data = ReadFile(f);
                const std::string name = fs::relative(f, root, ec).generic_string();
                if (!mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(),
                                           MZ_BEST_COMPRESSION)) {
                    ok = false;
                    break;
                }
            }
            void* buf = nullptr;
            size_t size = 0;
            if (ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size))
                out.assign(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + size);
            else
                ok = false;
            mz_zip_writer_end(&zip);
            return ok;
        }

        bool ManifestFromZip(const std::vector<uint8_t>& archive, std::string& manifest) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
                return false;
            bool found = false;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            int best = -1;
            size_t bestDepth = SIZE_MAX;
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                    continue;
                const std::string name = st.m_filename;
                if (name == "manifest.json" || name.ends_with("/manifest.json")) {
                    const size_t depth = static_cast<size_t>(std::count(name.begin(), name.end(), '/'));
                    if (depth < bestDepth) {
                        bestDepth = depth;
                        best = static_cast<int>(i);
                    }
                }
            }
            if (best >= 0) {
                size_t size = 0;
                if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(best), &size, 0)) {
                    manifest.assign(static_cast<char*>(data), size);
                    mz_free(data);
                    found = true;
                }
            }
            mz_zip_reader_end(&zip);
            return found;
        }

        std::vector<uint8_t> Sha256(const std::vector<uint8_t>& data) {
            std::vector<uint8_t> out(32);
            unsigned int len = 0;
            EVP_Digest(data.data(), data.size(), out.data(), &len, EVP_sha256(), nullptr);
            return out;
        }

        bool ParseUuid(const std::string& text, net::Uuid& out) {
            std::string hex;
            for (char c : text)
                if (std::isxdigit(static_cast<unsigned char>(c)))
                    hex += c;
            if (hex.size() != 32)
                return false;
            for (size_t i = 0; i < 16; i++)
                out.bytes[i] = static_cast<uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
            return true;
        }
    }

    bool ResourcePackManager::ReadPack(const std::string& path, ResourcePack& out, std::string& error) {
        std::error_code ec;
        const fs::path p(path);
        if (fs::is_directory(p, ec)) {
            if (!fs::is_regular_file(p / "manifest.json", ec)) {
                error = "no manifest.json";
                return false;
            }
            if (!ZipFolder(p, out.archive)) {
                error = "could not pack the folder";
                return false;
            }
        } else {
            out.archive = ReadFile(p);
        }
        std::string manifestText;
        if (!ManifestFromZip(out.archive, manifestText)) {
            error = "no manifest.json in the archive";
            return false;
        }
        json::Value manifest;
        if (!json::Parse(manifestText, manifest)) {
            error = "manifest.json is not valid json";
            return false;
        }
        const json::Value& header = manifest["header"];
        out.uuid = header["uuid"].String();
        out.name = header["name"].String(p.stem().string());
        const json::Value& version = header["version"];
        if (version.IsArray() && version.Size() >= 3)
            out.version = std::to_string(version[0].Int()) + "." + std::to_string(version[1].Int()) + "." +
                          std::to_string(version[2].Int());
        else
            out.version = version.String("1.0.0");
        const json::Value& modules = manifest["modules"];
        for (size_t i = 0; i < modules.Size(); i++) {
            const std::string type = modules[i]["type"].String();
            if (type == "data" || type == "script" || type == "javascript")
                out.behavior = true;
            if (type == "script" || type == "javascript")
                out.hasScripts = true;
        }
        net::Uuid check;
        if (!ParseUuid(out.uuid, check)) {
            error = "manifest.json has no valid header.uuid";
            return false;
        }
        out.sha256 = Sha256(out.archive);
        return true;
    }

    void ResourcePackManager::Load(const std::string& dataDir, bool mustAccept) {
        packs.clear();
        required = mustAccept;
        if (dataDir.empty())
            return;
        std::error_code ec;
        for (const char* folder : {"resource_packs", "behavior_packs"}) {
            const fs::path dir = fs::path(dataDir) / folder;
            if (!fs::is_directory(dir, ec)) {
                fs::create_directories(dir, ec);
                continue;
            }
            std::vector<fs::path> entries;
            for (const auto& e : fs::directory_iterator(dir, ec))
                entries.push_back(e.path());
            std::sort(entries.begin(), entries.end());
            for (const fs::path& e : entries) {
                const std::string ext = e.extension().string();
                if (!fs::is_directory(e, ec) && ext != ".zip" && ext != ".mcpack")
                    continue;
                ResourcePack pack;
                std::string error;
                if (!ReadPack(e.string(), pack, error)) {
                    LogWarn("pack " + e.filename().string() + " skipped: " + error);
                    continue;
                }
                if (const fs::path key = fs::path(e.string() + ".key"); fs::is_regular_file(key, ec)) {
                    std::ifstream in(key);
                    std::getline(in, pack.contentKey);
                }
                if (Find(pack.uuid)) {
                    LogWarn("pack " + e.filename().string() + " skipped: uuid " + pack.uuid +
                            " is already loaded");
                    continue;
                }
                LogInfo("pack " + pack.name + " " + pack.version + " (" +
                        std::to_string(pack.archive.size() / 1024) + " KiB)" +
                        (pack.behavior ? " [behavior]" : ""));
                packs.push_back(std::move(pack));
            }
        }
    }

    const ResourcePack* ResourcePackManager::Find(const std::string& packId) const {
        const std::string uuid = packId.substr(0, packId.find('_'));
        for (const ResourcePack& p : packs)
            if (p.uuid == uuid)
                return &p;
        return nullptr;
    }

    void ResourcePackManager::FillInfo(proto2225::ResourcePacksInfoPacket& info) const {
        info.mustAccept = required && !packs.empty();
        for (const ResourcePack& p : packs) {
            if (p.hasScripts)
                info.hasScripts = true;
            if (p.behavior)
                info.hasAddons = true;
            proto2225::TexturePackInfosEntry e;
            ParseUuid(p.uuid, e.uuid);
            e.version = p.version;
            e.size = p.archive.size();
            e.contentKey = p.contentKey;
            e.contentIdentity = p.contentKey.empty() ? std::string() : p.uuid;
            e.hasScripts = p.hasScripts;
            e.addonPack = p.behavior;
            info.texturePacks.push_back(std::move(e));
        }
    }

    void ResourcePackManager::FillStack(proto2225::ResourcePackStackPacket& stack) const {
        stack.mustAccept = required && !packs.empty();
        stack.gameVersion = "*";
        for (const ResourcePack& p : packs) {
            proto2225::ResourcePackIdVersionsEntry e;
            e.uuid = p.uuid;
            e.version = p.version;
            stack.resourcePacks.push_back(std::move(e));
        }
    }

    bool ResourcePackManager::DataInfo(const std::string& packId,
                                       proto2225::ResourcePackDataInfoPacket& out) const {
        const ResourcePack* p = Find(packId);
        if (!p)
            return false;
        out.packId = p->uuid + "_" + p->version;
        out.maxChunkSize = kChunkSize;
        out.chunkCount = static_cast<uint32_t>((p->archive.size() + kChunkSize - 1) / kChunkSize);
        out.size = p->archive.size();
        out.hash = p->sha256;
        out.isPremium = false;
        out.packType = p->behavior ? proto2225::ResourcePackDataInfoPacketPackType::Behavior
                                   : proto2225::ResourcePackDataInfoPacketPackType::Resources;
        return true;
    }

    bool ResourcePackManager::Chunk(const std::string& packId, uint32_t index,
                                    proto2225::ResourcePackChunkDataPacket& out) const {
        const ResourcePack* p = Find(packId);
        if (!p)
            return false;
        const uint64_t offset = static_cast<uint64_t>(index) * kChunkSize;
        if (offset >= p->archive.size())
            return false;
        const size_t len = static_cast<size_t>(std::min<uint64_t>(kChunkSize, p->archive.size() - offset));
        out.packId = p->uuid + "_" + p->version;
        out.chunkIndex = index;
        out.progress = offset;
        out.payload.assign(p->archive.begin() + static_cast<std::ptrdiff_t>(offset),
                           p->archive.begin() + static_cast<std::ptrdiff_t>(offset + len));
        return true;
    }
}
