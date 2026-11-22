#include "world/actor/mob/MobRegistry.hpp"
#include "world/actor/ActorManager.hpp"

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

    size_t MobRegistry::ApplyVanilla(const VanillaEntities& vanilla) {
        size_t matched = 0;
        for (MobType& type : types) {
            const VanillaEntity* v = vanilla.Get(type.def.id);
            if (!v)
                continue;
            matched++;
            if (v->width > 0 && v->height > 0) {
                type.def.width = v->width;
                type.def.height = v->height;
            }
            if (v->health > 0)
                type.def.health = v->health;
            if (v->speed > 0)
                type.def.speed = v->speed;
            if (v->damage >= 0)
                type.def.damage = v->damage;
            type.def.scale = v->scale > 0 ? v->scale : 1.0f;
            type.def.babyScale = v->babyScale;
            type.def.babyWidth = v->babyWidth;
            type.def.babyHeight = v->babyHeight;
            type.sizes = v->sizes;
            type.variants = v->variants;
        }
        return matched;
    }

    MobBody MobRegistry::BodyOf(const Mob& mob) const {
        MobBody body;
        if (!mob.def)
            return body;
        const MobDef& d = *mob.def;
        const MobType* type = TypeOf(mob.def);
        if (type && mob.size >= 0 && mob.size < static_cast<int>(type->sizes.size())) {
            // Size classes keep scale 1: the client sizes the model from the variant.
            const VanillaSize& s = type->sizes[static_cast<size_t>(mob.size)];
            return {s.width, s.height, 1.0f};
        }
        body = {d.width, d.height, d.scale > 0 ? d.scale : 1.0f};
        if (mob.baby) {
            body.width = d.babyWidth > 0 ? d.babyWidth : d.width * 0.5f;
            body.height = d.babyHeight > 0 ? d.babyHeight : d.height * 0.5f;
            body.scale = d.babyScale > 0 ? d.babyScale : body.scale * 0.5f;
        }
        return body;
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
