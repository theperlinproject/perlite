#include "server/VanillaPack.hpp"

#include "common/FileIo.hpp"
#include "common/Https.hpp"
#include "common/Json.hpp"
#include "common/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace perlite::vanillapack {
    namespace fs = std::filesystem;

    namespace {
        // The folders Perlite reads: mob data and block shapes.
        constexpr const char* kFolders[] = {"entities", "shapes"};
        constexpr const char* kMarker = ".source";

        bool Complete(const fs::path& dir) {
            std::error_code ec;
            for (const char* f : kFolders)
                if (!fs::is_directory(dir / f, ec))
                    return false;
            return true;
        }

        std::string Marker(const fs::path& dir) {
            std::string text;
            if (!file::Read(dir / kMarker, text))
                return {};
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            return text;
        }

        std::string Escape(const std::string& path) {
            static const char* hex = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : path) {
                if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                    out += static_cast<char>(c);
                else
                    out += std::string("%") + hex[c >> 4] + hex[c & 15];
            }
            return out;
        }

        // Lists every file under behavior_pack/<folder> through the GitHub contents API.
        bool List(https::Client& api, const std::string& folder, std::vector<std::string>& files,
                  std::string& error) {
            https::Response r;
            const std::string path = "/repos/" + std::string(kRepository) + "/contents/behavior_pack/" +
                                     Escape(folder) + "?ref=" + kRef;
            if (!api.Get(path, r, &error))
                return false;
            if (r.status != 200) {
                error = "GitHub answered " + std::to_string(r.status) + " for behavior_pack/" + folder;
                return false;
            }
            json::Value list;
            if (!json::Parse(r.body, list) || !list.IsArray()) {
                error = "GitHub sent an unreadable listing of behavior_pack/" + folder;
                return false;
            }
            for (const json::Value& e : list.items) {
                const std::string name = e["name"].String();
                const std::string type = e["type"].String();
                if (name.empty() || name.find("..") != std::string::npos || name.find('/') != std::string::npos)
                    continue;
                if (type == "file")
                    files.push_back(folder + "/" + name);
                else if (type == "dir" && !List(api, folder + "/" + name, files, error))
                    return false;
            }
            return true;
        }

        bool Download(const fs::path& target, std::string& error) {
            const auto began = std::chrono::steady_clock::now();
            https::Client api("api.github.com");
            std::vector<std::string> files;
            for (const char* folder : kFolders)
                if (!List(api, folder, files, error))
                    return false;
            const fs::path part = target.string() + ".part";
            std::error_code ec;
            fs::remove_all(part, ec);
            fs::create_directories(part, ec);
            https::Client raw("raw.githubusercontent.com");
            const std::string prefix = "/" + std::string(kRepository) + "/" + kRef + "/behavior_pack/";
            size_t done = 0;
            auto lastNote = std::chrono::steady_clock::now();
            for (const std::string& file : files) {
                https::Response r;
                if (!raw.Get(prefix + Escape(file), r, &error))
                    return false;
                if (r.status != 200) {
                    error = "GitHub answered " + std::to_string(r.status) + " for " + file;
                    return false;
                }
                const fs::path out = part / fs::path(file);
                fs::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary);
                f.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
                if (!f) {
                    error = "could not write " + out.string();
                    return false;
                }
                done++;
                if (std::chrono::steady_clock::now() - lastNote > std::chrono::seconds(2)) {
                    lastNote = std::chrono::steady_clock::now();
                    LogInfo("Downloading the vanilla behavior pack: " + std::to_string(done * 100 / files.size()) +
                            "%");
                }
            }
            {
                std::ofstream m(part / kMarker);
                m << kRepository << " " << kRef << "\n";
            }
            fs::remove_all(target, ec);
            fs::rename(part, target, ec);
            if (ec) {
                error = "could not move the pack into " + target.string() + ": " + ec.message();
                return false;
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                  began).count();
            LogInfo("Downloaded the vanilla behavior pack: " + std::to_string(files.size()) + " files in " +
                    std::to_string(ms) + " ms");
            return true;
        }
    }

    std::string Ensure(const std::string& dataDir, bool download) {
        std::error_code ec;
        if (const char* env = std::getenv("PERLITE_VANILLA_PACK"); env && *env) {
            if (Complete(env))
                return env;
            LogWarn(std::string("PERLITE_VANILLA_PACK=") + env + " has no entities and shapes folders; ignoring it");
        }
        const fs::path dir = fs::path(dataDir.empty() ? "." : dataDir) / "vanilla_behavior_pack";
        const std::string want = std::string(kRepository) + " " + kRef;
        const bool have = Complete(dir);
        if (have && Marker(dir) == want)
            return dir.string();
        if (const char* offline = std::getenv("PERLITE_OFFLINE"); offline && *offline && *offline != '0')
            download = false;
        if (!download) {
            if (!have)
                return {};
            LogWarn("The vanilla behavior pack in " + dir.string() + " is from another version; using it anyway");
            return dir.string();
        }
        LogInfo("Downloading the vanilla behavior pack (" + want + ") into " + dir.string());
        std::string error;
        if (Download(dir, error))
            return dir.string();
        fs::remove_all(dir.string() + ".part", ec);
        LogWarn("Could not download the vanilla behavior pack: " + error);
        return have ? dir.string() : std::string();
    }
}

#include "common/Json.hpp"

#include <miniz.h>

namespace perlite::vanillapack {
    namespace {
        constexpr const char* kLinks = "https://net-secondary.web.minecraft-services.net/api/v1.0/download/links";
        constexpr const char* kKeep[] = {"behavior_packs/vanilla/structures/", "behavior_packs/vanilla/loot_tables/"};

        bool HasStructures(const fs::path& dir) {
            std::error_code ec;
            return fs::is_directory(dir / "structures" / "village", ec) && fs::is_directory(dir / "loot_tables", ec);
        }
    }

    bool ExtractStructures(const std::string& zipBytes, const std::string& target, size_t& kept, std::string& error) {
        std::error_code ec;
        const fs::path dir(target);
        mz_zip_archive archive{};
        if (!mz_zip_reader_init_mem(&archive, zipBytes.data(), zipBytes.size(), 0)) {
            error = "not a zip file";
            return false;
        }
        const fs::path part = dir.string() + ".part";
        fs::remove_all(part, ec);
        kept = 0;
        const mz_uint count = mz_zip_reader_get_num_files(&archive);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&archive, i, &st) || st.m_is_directory)
                continue;
            std::string name = st.m_filename;
            std::replace(name.begin(), name.end(), '\\', '/');
            std::string rest;
            for (const char* keep : kKeep)
                if (const size_t at = name.find(keep); at != std::string::npos)
                    rest = name.substr(at + std::string_view("behavior_packs/vanilla/").size());
            if (rest.empty() || rest.contains(".."))
                continue;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
            if (!data)
                continue;
            const fs::path out = part / rest;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            mz_free(data);
            kept++;
        }
        mz_zip_reader_end(&archive);
        if (!HasStructures(part)) {
            fs::remove_all(part, ec);
            error = "no structure templates in it";
            return false;
        }
        fs::remove_all(dir, ec);
        fs::rename(part, dir, ec);
        return HasStructures(dir);
    }

    std::string EnsureStructures(const std::string& dataDir, bool download) {
        std::error_code ec;
        const fs::path dir = fs::path(dataDir.empty() ? "." : dataDir) / "bds_vanilla_pack";
        if (HasStructures(dir))
            return dir.string();
        if (const char* offline = std::getenv("PERLITE_OFFLINE"); offline && *offline && *offline != '0')
            download = false;
        if (!download)
            return {};
        std::string error;
        https::Response links;
        if (!https::Get(kLinks, links, &error) || links.status != 200) {
            LogWarn("Could not look up the Bedrock Dedicated Server download: " + (error.empty() ? "HTTP " + std::to_string(links.status) : error));
            return {};
        }
        json::Value doc;
        std::string url;
        if (json::Parse(links.body, doc))
            for (const json::Value& l : doc["result"]["links"].items)
                if (l["downloadType"].String() == "serverBedrockLinux")
                    url = l["downloadUrl"].String();
        if (url.empty()) {
            LogWarn("The Bedrock Dedicated Server download list has no Linux server");
            return {};
        }
        LogInfo("Downloading " + url + " for the vanilla structure templates and loot tables (the Minecraft EULA "
                "applies: https://www.minecraft.net/eula)");
        const auto began = std::chrono::steady_clock::now();
        https::Response zip;
        if (!https::Get(url, zip, &error) || zip.status != 200) {
            LogWarn("Could not download the Bedrock Dedicated Server: " + (error.empty() ? "HTTP " + std::to_string(zip.status) : error));
            return {};
        }
        size_t kept = 0;
        if (!ExtractStructures(zip.body, dir, kept, error)) {
            LogWarn("Could not keep the structures from the Bedrock Dedicated Server download: " + error);
            return {};
        }
        {
            std::ofstream marker(dir / "source.txt");
            marker << url << '\n';
        }
        LogInfo("Kept " + std::to_string(kept) + " structure and loot table files from the server in " + dir.string() + " (" +
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count()) +
                " s)");
        return HasStructures(dir) ? dir.string() : std::string();
    }
}
