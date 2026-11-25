#include "world/level/levelgen/bedrock/Pack.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlite::bedrock {

    namespace fs = std::filesystem;

    namespace {
        bool ReadJson(const fs::path& path, json::Value& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            return json::Parse(ss.str(), out);
        }

        // Identifier of a feature or rule document: the description identifier inside its one top-level object.
        std::string Identifier(const json::Value& doc) {
            for (const auto& [key, body] : doc.members)
                if (key != "format_version" && body.IsObject()) {
                    const std::string id = body["description"]["identifier"].String();
                    if (!id.empty())
                        return id;
                }
            return {};
        }

        void LoadFolder(const fs::path& dir, std::map<std::string, json::Value>& into) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json")
                    continue;
                json::Value doc;
                if (!ReadJson(entry.path(), doc))
                    continue;
                std::string id = Identifier(doc);
                if (id.empty())
                    continue;
                into[id] = std::move(doc);
            }
        }
    }

    std::shared_ptr<const Pack> Pack::Load(const std::string& folder, std::string& error) {
        auto pack = std::make_shared<Pack>();
        std::error_code ec;
        const fs::path biomes = fs::path(folder) / "biomes";
        if (!fs::is_directory(biomes, ec)) {
            error = "the vanilla behavior pack at " + folder + " has no biomes folder";
            return nullptr;
        }
        for (const auto& entry : fs::directory_iterator(biomes, ec)) {
            const std::string name = entry.path().filename().string();
            const std::string suffix = ".biome.json";
            if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            json::Value doc;
            if (!ReadJson(entry.path(), doc))
                continue;
            pack->biomes[name.substr(0, name.size() - suffix.size())] = std::move(doc);
        }
        if (pack->biomes.empty()) {
            error = "the vanilla behavior pack at " + folder + " has no biome documents";
            return nullptr;
        }
        LoadFolder(fs::path(folder) / "features", pack->features);
        LoadFolder(fs::path(folder) / "feature_rules", pack->featureRules);
        return pack;
    }

    const json::Value* Pack::BiomeComponents(const std::string& name) const {
        const auto it = biomes.find(name);
        if (it == biomes.end())
            return nullptr;
        const json::Value& c = it->second["minecraft:biome"]["components"];
        return c.IsObject() ? &c : nullptr;
    }
}
