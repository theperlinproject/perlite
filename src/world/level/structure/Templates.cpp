#include "world/level/structure/Templates.hpp"

#include "common/FileIo.hpp"
#include "common/Log.hpp"
#include "world/level/structure/JavaNbt.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace perlite::structure {
    namespace fs = std::filesystem;

    std::string StripNamespace(std::string_view s) {
        if (const size_t colon = s.find(':'); colon != std::string_view::npos)
            s.remove_prefix(colon + 1);
        return std::string(s);
    }

    const std::vector<uint16_t>& Template::Bedrock(int turns) const {
        const size_t t = static_cast<size_t>(turns & 3);
        std::call_once(translated[t], [&] {
            bedrock[t].reserve(palette.size());
            for (const JavaState& s : palette)
                bedrock[t].push_back(ToBedrock(Rotated(s, static_cast<int>(t))));
        });
        return bedrock[t];
    }

    std::shared_ptr<TemplateStore> TemplateStore::Find(const std::string& configured, const std::string& dataDir) {
        std::vector<fs::path> candidates;
        if (!configured.empty()) {
            candidates.push_back(fs::path(configured) / "structures");
            candidates.push_back(fs::path(configured));
        } else {
            candidates.push_back(fs::path("behavior_packs") / "vanilla" / "structures");
            candidates.push_back(fs::path(dataDir) / "behavior_packs" / "vanilla" / "structures");
        }
        std::error_code ec;
        for (const fs::path& p : candidates)
            if (fs::is_directory(p / "village", ec) || fs::is_directory(p / "bastion", ec) || fs::is_directory(p / "igloo", ec))
                return std::make_shared<TemplateStore>(p.string());
        if (!configured.empty())
            LogWarn("perlite-structure-pack: no structure templates in " + configured);
        return nullptr;
    }

    namespace {
        // The two parts of a jigsaw orientation ("north_up"), or the old single facing.
        void Orientation(const JavaState& s, std::string& front, std::string& top) {
            const std::string o = s.Get("orientation");
            if (const size_t u = o.find('_'); u != std::string::npos) {
                front = o.substr(0, u);
                top = o.substr(u + 1);
                return;
            }
            front = s.Get("facing", "north");
            top = front == "up" ? "north" : front == "down" ? "south" : "up";
        }
    }

    std::shared_ptr<const Template> TemplateStore::Get(std::string_view nameIn) const {
        const std::string name = StripNamespace(nameIn);
        {
            std::lock_guard lock(mutex);
            if (const auto it = templates.find(name); it != templates.end())
                return it->second;
        }
        std::shared_ptr<Template> t;
        std::ifstream in(fs::path(dir) / (name + ".nbt"), std::ios::binary);
        if (in) {
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            nbt::Value root;
            if (ReadTemplateNbt(bytes, root)) {
                t = std::make_shared<Template>();
                t->name = name;
                if (const nbt::Value* size = root.Find("size"); size && size->list.size() == 3) {
                    t->sx = static_cast<int>(size->list[0].integer);
                    t->sy = static_cast<int>(size->list[1].integer);
                    t->sz = static_cast<int>(size->list[2].integer);
                }
                const nbt::Value* palette = root.Find("palette");
                if (!palette)
                    if (const nbt::Value* palettes = root.Find("palettes"); palettes && !palettes->list.empty())
                        palette = &palettes->list[0];
                if (palette)
                    for (const nbt::Value& p : palette->list) {
                        JavaState s;
                        s.name = p.String("Name");
                        if (const nbt::Value* props = p.Find("Properties"))
                            for (const auto& e : props->compound)
                                s.props.emplace_back(e.first, e.second.text);
                        const std::string bare = StripNamespace(s.name);
                        t->isAir.push_back(bare == "air");
                        t->isJigsaw.push_back(bare == "jigsaw");
                        t->isStructureBlock.push_back(bare == "structure_block");
                        t->palette.push_back(std::move(s));
                    }
                if (const nbt::Value* blocks = root.Find("blocks"))
                    for (const nbt::Value& b : blocks->list) {
                        const nbt::Value* pos = b.Find("pos");
                        if (!pos || pos->list.size() != 3)
                            continue;
                        TemplateBlock tb;
                        tb.x = static_cast<int16_t>(pos->list[0].integer);
                        tb.y = static_cast<int16_t>(pos->list[1].integer);
                        tb.z = static_cast<int16_t>(pos->list[2].integer);
                        tb.state = static_cast<uint16_t>(b.Int("state"));
                        if (tb.state >= t->palette.size())
                            continue;
                        if (const nbt::Value* tag = b.Find("nbt")) {
                            tb.nbt = static_cast<int32_t>(t->nbts.size());
                            t->nbts.push_back(*tag);
                        }
                        t->blocks.push_back(tb);
                    }
                // The game keeps blocks in three lists (whole blocks, other blocks, blocks with data),
                // each by y, x and z; the jigsaws come out of the last in that order.
                std::ranges::stable_sort(t->blocks, [](const TemplateBlock& a, const TemplateBlock& b) {
                    const bool an = a.nbt >= 0, bn = b.nbt >= 0;
                    if (an != bn)
                        return !an;
                    if (a.y != b.y)
                        return a.y < b.y;
                    if (a.x != b.x)
                        return a.x < b.x;
                    return a.z < b.z;
                });
                for (const TemplateBlock& b : t->blocks) {
                    if (t->isJigsaw[b.state]) {
                        Jigsaw j;
                        j.x = b.x;
                        j.y = b.y;
                        j.z = b.z;
                        Orientation(t->palette[b.state], j.front, j.top);
                        if (b.nbt >= 0) {
                            const nbt::Value& n = t->nbts[static_cast<size_t>(b.nbt)];
                            const std::string old = n.String("attachement_type");
                            j.name = n.String("name", old);
                            j.target = n.String("target", old);
                            j.pool = n.String("pool", n.String("target_pool", "minecraft:empty"));
                            j.finalState = n.String("final_state", "minecraft:air");
                            const std::string joint = n.String("joint");
                            j.rollable = joint.empty() ? (j.front == "up" || j.front == "down") : joint == "rollable";
                            j.selectionPriority = static_cast<int>(n.Int("selection_priority"));
                        }
                        t->jigsaws.push_back(j);
                    } else if (t->isStructureBlock[b.state] && b.nbt >= 0) {
                        const nbt::Value& n = t->nbts[static_cast<size_t>(b.nbt)];
                        if (n.String("mode", "DATA") == "DATA" || t->palette[b.state].Get("mode") == "data")
                            t->markers.push_back({b.x, b.y, b.z, n.String("metadata")});
                    }
                }
                if (const nbt::Value* entities = root.Find("entities"))
                    for (const nbt::Value& e : entities->list) {
                        const nbt::Value* pos = e.Find("pos");
                        const nbt::Value* tag = e.Find("nbt");
                        if (!pos || pos->list.size() != 3 || !tag)
                            continue;
                        t->entities.push_back({pos->list[0].real, pos->list[1].real, pos->list[2].real, tag->String("id")});
                    }
            }
        }
        std::lock_guard lock(mutex);
        return templates[name] = t;
    }

    const std::vector<std::string>& TemplateStore::Pool(std::string_view poolIn) const {
        const std::string pool = StripNamespace(poolIn);
        std::lock_guard lock(mutex);
        if (const auto it = pools.find(pool); it != pools.end())
            return it->second;
        std::vector<std::string>& list = pools[pool];
        std::error_code ec;
        if (pool != "empty" && fs::is_directory(fs::path(dir) / pool, ec))
            for (const auto& e : fs::directory_iterator(fs::path(dir) / pool, ec))
                if (e.is_regular_file() && e.path().extension() == ".nbt")
                    list.push_back(pool + "/" + e.path().stem().string());
        std::ranges::sort(list);
        return list;
    }

    std::string TemplateStore::Fallback(std::string_view poolIn) const {
        const std::string pool = StripNamespace(poolIn);
        const size_t slash = pool.rfind('/');
        if (slash == std::string::npos || pool.ends_with("/terminators"))
            return "empty";
        const std::string sibling = pool.substr(0, slash) + "/terminators";
        std::error_code ec;
        return fs::is_directory(fs::path(dir) / sibling, ec) ? sibling : "empty";
    }

    int TemplateStore::MaxHeight(std::string_view poolIn) const {
        const std::string pool = StripNamespace(poolIn);
        {
            std::lock_guard lock(mutex);
            if (const auto it = heights.find(pool); it != heights.end())
                return it->second;
        }
        int h = 0;
        for (const std::string& name : Pool(pool))
            if (const auto t = Get(name))
                h = std::max(h, t->sy);
        const std::string fallback = Fallback(pool);
        if (fallback != "empty")
            for (const std::string& name : Pool(fallback))
                if (const auto t = Get(name))
                    h = std::max(h, t->sy);
        std::lock_guard lock(mutex);
        return heights[pool] = h;
    }
}
