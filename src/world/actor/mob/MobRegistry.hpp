#pragma once
#include "world/actor/mob/MobDef.hpp"
#include "world/actor/mob/VanillaEntities.hpp"

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace perlite {

    struct Mob;

    enum class MobCategory { Passive, Neutral, Hostile, Boss, Other };

    class MobBehavior {
    public:
        virtual ~MobBehavior() = default;
        virtual bool AutoTargets(const Mob&) const { return true; }
        virtual float ExplosionPower(const Mob&) const { return 3.0f; }
    };

    struct MobType {
        MobDef def;
        MobCategory category = MobCategory::Passive;
        std::unique_ptr<MobBehavior> behavior;
        std::vector<VanillaSize> sizes; // size classes from the behavior pack (slime, magma cube)
        std::vector<int> variants;      // appearance variants picked at spawn (rabbit coats)
    };

    // A mob's current hitbox and model scale, from its type, size class and age.
    struct MobBody {
        float width = 0.6f, height = 1.8f, scale = 1.0f;
    };

    class MobRegistry {
    public:
        static MobRegistry& Instance();

        const MobType& Add(const MobDef& def, MobCategory category,
                           std::unique_ptr<MobBehavior> behavior = nullptr);
        void Alias(std::string_view alias, std::string_view identifier);
        const MobType* Find(std::string_view identifier) const;
        const MobType* TypeOf(const MobDef* def) const;
        const MobBehavior& BehaviorOf(const MobDef* def) const;
        const std::deque<MobType>& All() const { return types; }
        // Updates every registered mob from the vanilla behavior pack. Returns how many matched.
        size_t ApplyVanilla(const VanillaEntities& vanilla);
        MobBody BodyOf(const Mob& mob) const;

    private:
        MobRegistry();
        std::deque<MobType> types;
        std::unordered_map<std::string, size_t> byId;
        std::unordered_map<std::string, std::string> aliases;
    };

    void RegisterVanillaMobs(MobRegistry& registry);
}
