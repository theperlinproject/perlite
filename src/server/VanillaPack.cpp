#include "server/VanillaPack.hpp"

#include "common/FileIo.hpp"
#include "common/Https.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <vector>

namespace perlite::vanillapack {
    namespace fs = std::filesystem;

    namespace {
        constexpr const char* kLinks = "https://net-secondary.web.minecraft-services.net/api/v1.0/download/links";
        constexpr const char* kMarker = ".source";
        // Bumped when the layout of the kept pack changes, so an older copy is rebuilt.
        constexpr const char* kLayout = "bds-pack 2";

        bool Complete(const fs::path& dir) {
            std::error_code ec;
            return fs::is_directory(dir / "entities", ec) && fs::is_directory(dir / "shapes", ec);
        }

        std::string Marker(const fs::path& dir) {
            std::string text;
            if (!file::Read(dir / kMarker, text))
                return {};
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            return text;
        }

        // "behavior_packs/vanilla_1.21.30/" -> {1, 21, 30}; the base pack sorts first.
        std::vector<int> PackVersion(const std::string& pack) {
            std::vector<int> v;
            if (pack == "vanilla")
                return v;
            size_t at = std::strlen("vanilla_");
            while (at < pack.size()) {
                v.push_back(std::atoi(pack.c_str() + at));
                const size_t dot = pack.find('.', at);
                if (dot == std::string::npos)
                    break;
                at = dot + 1;
            }
            return v;
        }

        bool IsVanillaPack(const std::string& pack) {
            if (pack == "vanilla")
                return true;
            if (!pack.starts_with("vanilla_") || pack.size() <= 8)
                return false;
            for (size_t i = 8; i < pack.size(); i++)
                if (!std::isdigit(static_cast<unsigned char>(pack[i])) && pack[i] != '.')
                    return false;
            return true;
        }

        bool Safe(const std::string& rel) {
            return !rel.empty() && !rel.contains("..") && rel.front() != '/' && !rel.contains(':') && !rel.contains('\\');
        }

        // The newest server's version from its download name ("bedrock-server-1.26.66.1.zip").
        std::string VersionOf(const std::string& url) {
            const size_t at = url.rfind("bedrock-server-");
            if (at == std::string::npos)
                return url;
            std::string v = url.substr(at + 15);
            if (v.ends_with(".zip"))
                v.resize(v.size() - 4);
            return v;
        }

        bool LatestServer(std::string& url, std::string& error) {
            https::Response links;
            if (!https::Get(kLinks, links, &error) || links.status != 200) {
                if (error.empty())
                    error = "minecraft.net answered " + std::to_string(links.status);
                return false;
            }
            json::Value doc;
            if (json::Parse(links.body, doc))
                for (const json::Value& l : doc["result"]["links"].items)
#ifdef _WIN32
                    if (l["downloadType"].String() == "serverBedrockWindows")
#else
                    if (l["downloadType"].String() == "serverBedrockLinux")
#endif
                        url = l["downloadUrl"].String();
            if (url.empty()) {
                error = "the download list has no dedicated server";
                return false;
            }
            return true;
        }

        // Streams the server zip to a file, with progress in the log.
        bool DownloadTo(const std::string& url, const fs::path& file, std::string& error) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "could not write " + file.string();
                return false;
            }
            https::Response r;
            long long got = 0;
            int lastPercent = -1;
            auto lastNote = std::chrono::steady_clock::now();
            const https::Sink sink = [&](const char* data, size_t size) {
                if (!data)
                    return true;
                out.write(data, static_cast<std::streamsize>(size));
                got += static_cast<long long>(size);
                if (std::chrono::steady_clock::now() - lastNote > std::chrono::seconds(2)) {
                    lastNote = std::chrono::steady_clock::now();
                    if (r.length > 0) {
                        const int percent = static_cast<int>(got * 100 / r.length);
                        if (percent != lastPercent)
                            LogInfo("Downloading the Bedrock Dedicated Server: " + std::to_string(percent) + "% (" +
                                    std::to_string(got >> 20) + " of " + std::to_string(r.length >> 20) + " MiB)");
                        lastPercent = percent;
                    } else {
                        LogInfo("Downloading the Bedrock Dedicated Server: " + std::to_string(got >> 20) + " MiB");
                    }
                }
                return static_cast<bool>(out);
            };
            if (!https::Get(url, r, &sink, &error))
                return false;
            if (r.status != 200) {
                error = "minecraft.net answered " + std::to_string(r.status);
                return false;
            }
            out.close();
            if (!out) {
                error = "could not write " + file.string();
                return false;
            }
            if (r.length >= 0 && got != r.length) {
                error = "the download stopped after " + std::to_string(got) + " of " + std::to_string(r.length) + " bytes";
                return false;
            }
            return true;
        }
    }

    bool ReadArchive(const std::string& bytes, const std::function<void(const std::string&, std::string_view)>& each) {
        // The game's packed folder: an 8-byte magic, the entry count and a version, then 256-byte
        // entries (name length, name, offset and size of the contents), then the contents.
        constexpr uint64_t kMagic = 0x267052A0B125277DULL;
        if (bytes.size() < 16)
            return false;
        uint64_t magic = 0;
        std::memcpy(&magic, bytes.data(), 8);
        if (magic != kMagic)
            return false;
        uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 8, 4);
        const size_t base = 16 + static_cast<size_t>(count) * 256;
        if (base > bytes.size())
            return false;
        for (uint32_t i = 0; i < count; i++) {
            const char* e = bytes.data() + 16 + static_cast<size_t>(i) * 256;
            const size_t len = std::min<size_t>(static_cast<unsigned char>(e[0]), 247);
            const std::string name(e + 1, len);
            uint32_t offset = 0, size = 0;
            std::memcpy(&offset, e + 248, 4);
            std::memcpy(&size, e + 252, 4);
            if (base + static_cast<size_t>(offset) + size > bytes.size())
                return false;
            each(name, std::string_view(bytes.data() + base + offset, size));
        }
        return true;
    }

    bool HasStructures(const std::string& dir) {
        std::error_code ec;
        return !dir.empty() && fs::is_directory(fs::path(dir) / "structures" / "village", ec) &&
               fs::is_directory(fs::path(dir) / "loot_tables", ec);
    }

    bool ExtractPack(const std::string& zipPath, const std::string& target, size_t& kept, std::string& error) {
        std::error_code ec;
        mz_zip_archive archive{};
        if (!mz_zip_reader_init_file(&archive, zipPath.c_str(), 0)) {
            error = "not a zip file";
            return false;
        }
        // Every file of the vanilla packs, by pack, as (path inside the pack, zip index).
        struct Entry {
            std::string rel;
            mz_uint index;
        };
        std::map<std::vector<int>, std::pair<std::string, std::vector<Entry>>> packs;
        const mz_uint count = mz_zip_reader_get_num_files(&archive);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&archive, i, &st) || st.m_is_directory)
                continue;
            std::string name = st.m_filename;
            std::replace(name.begin(), name.end(), '\\', '/');
            const size_t at = name.find("behavior_packs/");
            if (at == std::string::npos)
                continue;
            const std::string inside = name.substr(at + 15);
            const size_t slash = inside.find('/');
            if (slash == std::string::npos)
                continue;
            const std::string pack = inside.substr(0, slash), rel = inside.substr(slash + 1);
            if (!IsVanillaPack(pack) || !Safe(rel))
                continue;
            auto& slot = packs[PackVersion(pack)];
            slot.first = pack;
            slot.second.push_back({rel, i});
        }
        if (packs.empty()) {
            mz_zip_reader_end(&archive);
            error = "no vanilla behavior pack in it";
            return false;
        }
        const fs::path part = target + ".part";
        fs::remove_all(part, ec);
        fs::create_directories(part, ec);
        kept = 0;
        std::set<std::string> written;
        const auto write = [&](const std::string& rel, std::string_view data) {
            if (!Safe(rel))
                return;
            const fs::path out = part / fs::path(rel);
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (written.insert(rel).second)
                kept++;
        };
        // Oldest first, so each newer pack replaces what it changes (the game's pack stack).
        for (auto& [version, pack] : packs) {
            (void)version;
            // Within a pack the loose files win over the packed copies of the same file.
            std::vector<Entry> loose, packed;
            for (const Entry& e : pack.second) {
                if (e.rel == "manifest.json" || e.rel == "manifest_publish.json" || e.rel == "pack_icon.png" ||
                    e.rel.find('/') == std::string::npos)
                    continue;
                (e.rel.starts_with("__brarchive/") ? packed : loose).push_back(e);
            }
            std::set<std::string> looseHere;
            for (const Entry& e : loose)
                looseHere.insert(e.rel);
            for (const Entry& e : packed) {
                if (!e.rel.ends_with(".brarchive"))
                    continue;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&archive, e.index, &size, 0);
                if (!data)
                    continue;
                const std::string bytes(static_cast<const char*>(data), size);
                mz_free(data);
                // __brarchive/loot_tables/chests.brarchive holds loot_tables/chests/*.
                std::string folder = e.rel.substr(std::strlen("__brarchive/"));
                folder.resize(folder.size() - std::strlen(".brarchive"));
                ReadArchive(bytes, [&](const std::string& name, std::string_view contents) {
                    const std::string rel = folder + "/" + name;
                    if (!looseHere.contains(rel))
                        write(rel, contents);
                });
            }
            for (const Entry& e : loose) {
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&archive, e.index, &size, 0);
                if (!data)
                    continue;
                write(e.rel, std::string_view(static_cast<const char*>(data), size));
                mz_free(data);
            }
        }
        mz_zip_reader_end(&archive);
        if (!Complete(part)) {
            fs::remove_all(part, ec);
            error = "the vanilla pack in it has no entities or block shapes";
            return false;
        }
        fs::remove_all(target, ec);
        fs::rename(part, target, ec);
        if (ec) {
            error = "could not move the pack into " + target + ": " + ec.message();
            return false;
        }
        return true;
    }

    std::string Ensure(const std::string& dataDir, bool download) {
        std::error_code ec;
        if (const char* env = std::getenv("PERLITE_VANILLA_PACK"); env && *env) {
            if (Complete(env))
                return env;
            LogWarn(std::string("PERLITE_VANILLA_PACK=") + env + " has no entities and shapes folders; ignoring it");
        }
        const fs::path base = fs::path(dataDir.empty() ? "." : dataDir);
        const fs::path dir = base / "vanilla_behavior_pack";
        const bool have = Complete(dir);
        const std::string marker = have ? Marker(dir) : std::string();
        const bool fromServer = marker.starts_with(kLayout);
        if (const char* offline = std::getenv("PERLITE_OFFLINE"); offline && *offline && *offline != '0')
            download = false;
        if (!download) {
            if (have && !fromServer)
                LogWarn("The vanilla behavior pack in " + dir.string() + " is from an older Perlite; using it anyway");
            return have ? dir.string() : std::string();
        }
        std::string error, url;
        if (!LatestServer(url, error)) {
            if (have)
                return dir.string(); // offline or minecraft.net is down: the kept copy does
            LogWarn("Could not look up the Bedrock Dedicated Server download: " + error);
            return {};
        }
        const std::string want = std::string(kLayout) + " " + VersionOf(url);
        if (have && marker == want)
            return dir.string();
        LogInfo("Downloading " + url + " for the vanilla behavior pack (the Minecraft EULA applies: "
                "https://www.minecraft.net/eula); only its behavior packs are kept");
        const auto began = std::chrono::steady_clock::now();
        fs::create_directories(base, ec);
        const fs::path zip = base / "bedrock-server-download.zip";
        if (!DownloadTo(url, zip, error)) {
            fs::remove(zip, ec);
            LogWarn("Could not download the Bedrock Dedicated Server: " + error);
            return have ? dir.string() : std::string();
        }
        size_t kept = 0;
        const bool ok = ExtractPack(zip.string(), dir.string(), kept, error);
        fs::remove(zip, ec);
        if (!ok) {
            LogWarn("Could not take the vanilla behavior pack from the Bedrock Dedicated Server: " + error);
            return have ? dir.string() : std::string();
        }
        {
            std::ofstream m(dir / kMarker);
            m << want << '\n';
        }
        LogInfo("Kept the vanilla behavior pack of Bedrock Dedicated Server " + VersionOf(url) + ": " + std::to_string(kept) +
                " files in " +
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count()) +
                " s");
        return dir.string();
    }
}
