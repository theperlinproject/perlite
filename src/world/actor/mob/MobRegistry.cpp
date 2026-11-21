#include "world/actor/mob/MobRegistry.hpp"

namespace perlite {

    MobRegistry& MobRegistry::Instance() {
        static MobRegistry registry;
        return registry;
    }

    MobRegistry::MobRegistry() {
        RegisterVanillaMobs(*this);
        Alias("minecraft:villager", "minecraft:villager_v2");
        Alias("minecraft:zombie_villager", "minecraft:zombie_villager_v2");
        Alias("minecraft:zombified_piglin", "minecraft:zombie_pigman");
        Alias("minecraft:evoker", "minecraft:evocation_illager");
        Alias("minecraft:tropical_fish", "minecraft:tropicalfish");
    }

    const MobType& MobRegistry::Add(const MobDef& def, MobCategory category,
                                    std::unique_ptr<MobBehavior> behavior) {
        if (const auto it = byId.find(def.id); it != byId.end()) {
            MobType& existing = types[it->second];
            existing.def = def;
            existing.category = category;
            existing.behavior = std::move(behavior);
            return existing;
        }
        byId.emplace(def.id, types.size());
        types.push_back({def, category, std::move(behavior)});
        return types.back();
    }

    void MobRegistry::Alias(std::string_view alias, std::string_view identifier) {
        aliases[std::string(alias)] = std::string(identifier);
    }

    const MobType* MobRegistry::Find(std::string_view identifier) const {
        std::string want(identifier);
        if (!want.contains(':'))
            want = "minecraft:" + want;
        if (const auto a = aliases.find(want); a != aliases.end())
            want = a->second;
        const auto it = byId.find(want);
        return it == byId.end() ? nullptr : &types[it->second];
    }

    const MobType* MobRegistry::TypeOf(const MobDef* def) const {
        if (!def)
            return nullptr;
        const auto it = byId.find(def->id);
        return it == byId.end() || &types[it->second].def != def ? nullptr : &types[it->second];
    }

    const MobBehavior& MobRegistry::BehaviorOf(const MobDef* def) const {
        static const MobBehavior fallback;
        const MobType* type = TypeOf(def);
        return type && type->behavior ? *type->behavior : fallback;
    }

    const MobDef* FindMob(const char* identifier) {
        const MobType* type = MobRegistry::Instance().Find(identifier);
        return type ? &type->def : nullptr;
    }
}
