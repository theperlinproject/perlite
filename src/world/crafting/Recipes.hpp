#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlite {

    struct RecipeIngredient {
        std::string item;
        int count = 1;
    };

    struct CraftRecipe {
        uint32_t netId = 0;
        bool shaped = false;
        int width = 0, height = 0;
        std::vector<RecipeIngredient> input;
        std::string output;
        int16_t outputId = 0;
        int outputCount = 1;
        int outputData = 0;
        std::string block = "crafting_table"; // the station: crafting_table, stonecutter, smithing_table
        bool smithing = false;
        bool trim = false; // an armour trim: the result is the base with the trim of the template and material                // input is template, base, addition; the base's data carries over
    };

    struct SmeltRecipe {
        bool furnace = false, blast = false, smoker = false, campfire = false;
        std::string input;
        std::string output;
        int16_t outputId = 0;
        int outputCount = 1;
    };

    class Recipes {
    public:
        static Recipes& Instance();
        void Load();
        // The vanilla behavior pack whose recipes folder adds the station recipes (read by Load).
        void SetPackDir(const std::string& dir) { packDir = dir; }
        const CraftRecipe* ByNetId(uint32_t id) const;
        const SmeltRecipe* Smelt(const std::string& item, int station) const;
        const std::vector<CraftRecipe>& Crafting() const { return crafting; }
        const std::vector<uint8_t>& CraftingDataBody() const { return body; }
        static bool Matches(const std::string& ingredient, const std::string& item);
        static int FuelTicks(const std::string& item);

    private:
        bool loaded = false;
        std::string packDir;
        void LoadPackRecipes();
        std::vector<CraftRecipe> crafting;
        std::vector<SmeltRecipe> smelting;
        std::vector<uint8_t> body;
    };
}
