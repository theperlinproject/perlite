#pragma once
// The vanilla behavior-pack documents the generator reads (biomes, features, feature rules), loaded from the
// pack folder Perlite installs at run time. Nothing here is compiled in.

#include "common/Json.hpp"

#include <map>
#include <memory>
#include <string>

namespace perlite::bedrock {

    class Pack {
    public:
        // Loads <folder>/biomes/*.biome.json, features/*.json and feature_rules/*.json. Returns null when the folder
        // has no biome documents.
        static std::shared_ptr<const Pack> Load(const std::string& folder, std::string& error);

        // The "components" object of a biome document by its pack name, or null.
        const json::Value* BiomeComponents(const std::string& pack) const;
        const std::map<std::string, json::Value>& Features() const { return features; }
        const std::map<std::string, json::Value>& FeatureRules() const { return featureRules; }

    private:
        std::map<std::string, json::Value> biomes;  // pack name -> whole document
        std::map<std::string, json::Value> features;
        std::map<std::string, json::Value> featureRules;
    };
}
