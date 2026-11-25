#include "world/level/levelgen/bedrock/BlockTraits.hpp"

#include "world/level/block/BlockCatalog.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace perlite::bedrock {

    IVec3 Offset(Direction d) {
        switch (d) {
        case Direction::Down:
            return {0, -1, 0};
        case Direction::Up:
            return {0, 1, 0};
        case Direction::North:
            return {0, 0, -1};
        case Direction::South:
            return {0, 0, 1};
        case Direction::West:
            return {-1, 0, 0};
        case Direction::East:
            return {1, 0, 0};
        }
        return {};
    }

    Direction Opposite(Direction d) {
        switch (d) {
        case Direction::Down:
            return Direction::Up;
        case Direction::Up:
            return Direction::Down;
        case Direction::North:
            return Direction::South;
        case Direction::South:
            return Direction::North;
        case Direction::West:
            return Direction::East;
        case Direction::East:
            return Direction::West;
        }
        return d;
    }

    const char* DirectionName(Direction d) {
        static const char* const names[] = {"down", "up", "north", "south", "west", "east"};
        return names[static_cast<int>(d)];
    }

    int VineBit(Direction face) {
        switch (face) {
        case Direction::South:
            return 1;
        case Direction::West:
            return 2;
        case Direction::North:
            return 4;
        case Direction::East:
            return 8;
        default:
            return 0;
        }
    }

    int MultifaceBit(Direction face) {
        switch (face) {
        case Direction::Down:
            return 1;
        case Direction::Up:
            return 2;
        case Direction::South:
            return 4;
        case Direction::West:
            return 8;
        case Direction::North:
            return 16;
        case Direction::East:
            return 32;
        }
        return 0;
    }

    Direction FromLegacyDirection(int value) {
        switch (value) {
        case 1:
            return Direction::West;
        case 2:
            return Direction::North;
        case 3:
            return Direction::East;
        default:
            return Direction::South;
        }
    }

    namespace {
        std::string ValueText(const StateValue& v) {
            if (const bool* b = std::get_if<bool>(&v))
                return *b ? "true" : "false";
            if (const int32_t* i = std::get_if<int32_t>(&v))
                return std::to_string(*i);
            return std::get<std::string>(v);
        }

        // A permutation condition: state comparisons joined by &&, || and !, with parentheses.
        struct Expr {
            enum Kind : uint8_t { Eq, Ne, And, Or, Not } kind = Eq;
            std::string key, value;
            std::unique_ptr<Expr> a, b;

            bool Eval(const StateList& states) const {
                switch (kind) {
                case Eq:
                case Ne: {
                    std::string have;
                    for (const auto& [k, v] : states)
                        if (k == key) {
                            have = ValueText(v);
                            break;
                        }
                    return (have == value) == (kind == Eq);
                }
                case And:
                    return a->Eval(states) && b->Eval(states);
                case Or:
                    return a->Eval(states) || b->Eval(states);
                case Not:
                    return !a->Eval(states);
                }
                return false;
            }
        };

        struct Parser {
            std::string_view s;
            size_t i = 0;
            std::unique_ptr<Expr> Or() {
                auto left = And();
                while (s.substr(i, 2) == "||") {
                    i += 2;
                    auto e = std::make_unique<Expr>();
                    e->kind = Expr::Or;
                    e->a = std::move(left);
                    e->b = And();
                    left = std::move(e);
                }
                return left;
            }
            std::unique_ptr<Expr> And() {
                auto left = Unary();
                while (s.substr(i, 2) == "&&") {
                    i += 2;
                    auto e = std::make_unique<Expr>();
                    e->kind = Expr::And;
                    e->a = std::move(left);
                    e->b = Unary();
                    left = std::move(e);
                }
                return left;
            }
            std::unique_ptr<Expr> Unary() {
                if (i < s.size() && s[i] == '!' && s.substr(i, 2) != "!=") {
                    i++;
                    auto e = std::make_unique<Expr>();
                    e->kind = Expr::Not;
                    e->a = Unary();
                    return e;
                }
                if (i < s.size() && s[i] == '(') {
                    i++;
                    auto e = Or();
                    if (i < s.size() && s[i] == ')')
                        i++;
                    return e;
                }
                auto e = std::make_unique<Expr>();
                size_t op = s.find_first_of("=!", i);
                e->key = std::string(s.substr(i, op - i));
                e->kind = s.substr(op, 2) == "!=" ? Expr::Ne : Expr::Eq;
                i = op + 2;
                size_t end = i;
                while (end < s.size() && s[end] != ')' && s[end] != '&' && s[end] != '|')
                    end++;
                e->value = std::string(s.substr(i, end - i));
                i = end;
                return e;
            }
        };

        // Traits one component list sets; unset ones keep what the base gave.
        struct Traits {
            int8_t solid = -1, support = -1, collision = -1, transparent = -1, replaceable = -1, liquid = -1;
            float box[6] = {0, 0, 0, 1, 1, 1};
            int16_t placement = -1, denied = -1;
            bool hasPlacement = false;
        };

        enum LiquidKind : int8_t { kContainsWater, kContainsBrokenWater, kContainsBrokenWaterBrokenLava, kBrokenWater, kBrokenLava, kBrokenWaterLava };

        struct Def {
            Traits base;
            std::vector<std::pair<std::unique_ptr<Expr>, Traits>> perms;
        };

        struct PlacementList {
            std::vector<std::pair<std::vector<PlacementFace>, std::vector<std::string>>> rules;
            std::vector<std::string> blocks; // a denied list
        };

        std::vector<std::string> Split(std::string_view s, char sep) {
            std::vector<std::string> out;
            size_t start = 0;
            while (start <= s.size()) {
                const size_t at = s.find(sep, start);
                out.emplace_back(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
                if (at == std::string_view::npos)
                    break;
                start = at + 1;
            }
            return out;
        }

        PlacementFace FaceOf(const std::string& f) {
            if (f == "down")
                return PlacementFace::Down;
            if (f == "up")
                return PlacementFace::Up;
            if (f == "north")
                return PlacementFace::North;
            if (f == "south")
                return PlacementFace::South;
            if (f == "east")
                return PlacementFace::East;
            return PlacementFace::West;
        }

        const std::vector<const char*>& TagMembers(Tag tag) {
            static const std::array<std::vector<const char*>, static_cast<size_t>(Tag::Count)> members = {{
                {"minecraft:air"},
                {"minecraft:brain_coral", "minecraft:brain_coral_fan", "minecraft:bubble_coral", "minecraft:bubble_coral_fan",
                 "minecraft:fire_coral", "minecraft:fire_coral_fan", "minecraft:horn_coral", "minecraft:horn_coral_fan",
                 "minecraft:tube_coral", "minecraft:tube_coral_fan"},
                {"minecraft:coarse_dirt", "minecraft:dirt", "minecraft:dirt_with_roots"},
                {"minecraft:bedrock", "minecraft:chest", "minecraft:end_portal_frame", "minecraft:reinforced_deepslate",
                 "minecraft:mob_spawner", "minecraft:trial_spawner", "minecraft:vault"},
                {"minecraft:acacia_leaves", "minecraft:azalea_leaves", "minecraft:birch_leaves", "minecraft:cherry_leaves",
                 "minecraft:dark_oak_leaves", "minecraft:azalea_leaves_flowered", "minecraft:jungle_leaves",
                 "minecraft:mangrove_leaves", "minecraft:oak_leaves", "minecraft:orange_poplar_leaves", "minecraft:pale_oak_leaves",
                 "minecraft:red_poplar_leaves", "minecraft:spruce_leaves", "minecraft:yellow_poplar_leaves"},
                {"minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:cherry_log",
                 "minecraft:cherry_wood", "minecraft:crimson_hyphae", "minecraft:crimson_stem", "minecraft:dark_oak_log",
                 "minecraft:dark_oak_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:mangrove_log",
                 "minecraft:mangrove_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:pale_oak_log",
                 "minecraft:pale_oak_wood", "minecraft:poplar_log", "minecraft:poplar_wood", "minecraft:spruce_log",
                 "minecraft:spruce_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood",
                 "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:stripped_cherry_log",
                 "minecraft:stripped_cherry_wood", "minecraft:stripped_crimson_hyphae", "minecraft:stripped_crimson_stem",
                 "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:stripped_jungle_log",
                 "minecraft:stripped_jungle_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood",
                 "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:stripped_pale_oak_log",
                 "minecraft:stripped_pale_oak_wood", "minecraft:stripped_poplar_log", "minecraft:stripped_poplar_wood",
                 "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:stripped_warped_hyphae",
                 "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:warped_stem"},
                {"minecraft:mud", "minecraft:muddy_mangrove_roots"},
                {"minecraft:air", "minecraft:bubble_column", "minecraft:bush", "minecraft:crimson_roots", "minecraft:deadbush",
                 "minecraft:fern", "minecraft:fire", "minecraft:glow_lichen", "minecraft:hanging_roots", "minecraft:large_fern",
                 "minecraft:lava", "minecraft:leaf_litter", "minecraft:nether_sprouts", "minecraft:red_shrub", "minecraft:resin_clump",
                 "minecraft:seagrass", "minecraft:short_dry_grass", "minecraft:short_grass", "minecraft:snow_layer",
                 "minecraft:soul_fire", "minecraft:structure_void", "minecraft:tall_dry_grass", "minecraft:tall_grass",
                 "minecraft:vine", "minecraft:warped_roots", "minecraft:water"},
                {"minecraft:red_sand", "minecraft:sand", "minecraft:suspicious_sand"},
                {"minecraft:cinnabar", "minecraft:sulfur"},
                {"minecraft:brain_coral_wall_fan", "minecraft:bubble_coral_wall_fan", "minecraft:fire_coral_wall_fan",
                 "minecraft:horn_coral_wall_fan", "minecraft:tube_coral_wall_fan"},
                {"minecraft:frosted_ice"},
            }};
            return members[static_cast<size_t>(tag)];
        }

        Support SupportOf(const std::string& name, uint32_t tags, bool hasPlacement) {
            static const std::unordered_map<std::string, Support> byName = {
                {"minecraft:reeds", Support::SugarCane},
                {"minecraft:cactus", Support::Cactus},
                {"minecraft:cactus_flower", Support::CactusFlower},
                {"minecraft:brown_mushroom", Support::Mushroom},
                {"minecraft:red_mushroom", Support::Mushroom},
                {"minecraft:leaf_litter", Support::SturdyFloor},
                {"minecraft:seagrass", Support::Seagrass},
                {"minecraft:sea_pickle", Support::SolidFloor},
                {"minecraft:kelp", Support::Kelp},
                {"minecraft:mangrove_propagule", Support::Propagule},
                {"minecraft:snow_layer", Support::SnowLayer},
                {"minecraft:moss_carpet", Support::AnyFloor},
                {"minecraft:pale_moss_carpet", Support::AnyFloor},
                {"minecraft:spore_blossom", Support::SporeBlossom},
                {"minecraft:hanging_roots", Support::SturdyCeiling},
                {"minecraft:pale_hanging_moss", Support::PaleHangingMoss},
                {"minecraft:waterlily", Support::Waterlily},
                {"minecraft:big_dripleaf", Support::BigDripleaf},
                {"minecraft:melon_block", Support::Anywhere},
                {"minecraft:pumpkin", Support::Anywhere},
                {"minecraft:carved_pumpkin", Support::Anywhere},
                {"minecraft:bubble_column", Support::Anywhere},
            };
            if (const auto it = byName.find(name); it != byName.end())
                return it->second;
            const auto has = [&](Tag t) { return (tags >> static_cast<int>(t)) & 1; };
            if (has(Tag::Leaves))
                return Support::Anywhere;
            if (has(Tag::WallCorals))
                return Support::WallCoral;
            if (has(Tag::Corals))
                return Support::Coral;
            if (hasPlacement)
                return Support::Placed;
            return Support::Anywhere;
        }
    }

    struct Blocks::Impl {
        std::unordered_map<std::string, Def> defs;
        std::vector<PlacementList> placements;
        std::unordered_map<std::string, int16_t> placementIndex;
        mutable std::mutex mutex;
        mutable std::unordered_map<std::string, uint16_t> kinds;
        mutable std::array<std::atomic<const BlockEntry*>, 0x10000> entries{};
        mutable std::vector<std::unique_ptr<BlockEntry>> owned;

        void ApplyTraits(Traits& into, const std::string& token) {
            const char k = token[0];
            const std::string v = token.substr(1);
            switch (k) {
            case 'S':
                into.solid = v == "1";
                break;
            case 'U':
                into.support = static_cast<int8_t>(std::atoi(v.c_str()));
                break;
            case 'C':
                into.collision = v == "1" ? 1 : 0;
                if (v == "1") {
                    const float cube[6] = {0, 0, 0, 1, 1, 1};
                    std::copy(cube, cube + 6, into.box);
                }
                break;
            case 'B': {
                into.collision = 2;
                const auto nums = Split(v, ',');
                for (size_t i = 0; i < 6 && i < nums.size(); i++)
                    into.box[i] = std::strtof(nums[i].c_str(), nullptr);
                break;
            }
            case 'L':
                into.liquid = v == "cw" ? kContainsWater : v == "cbw" ? kContainsBrokenWater : v == "cbwbl" ? kContainsBrokenWaterBrokenLava
                              : v == "bw" ? kBrokenWater : v == "bl" ? kBrokenLava : kBrokenWaterLava;
                break;
            case 'T':
                into.transparent = v == "1";
                break;
            case 'R':
                into.replaceable = v == "1";
                break;
            case 'P': {
                into.hasPlacement = true;
                const size_t slash = v.find('/');
                const std::string rules = v.substr(0, slash);
                if (!rules.empty())
                    into.placement = placementIndex.contains(rules) ? placementIndex[rules] : -1;
                if (slash != std::string::npos) {
                    const std::string denied = v.substr(slash + 1);
                    into.denied = placementIndex.contains(denied) ? placementIndex[denied] : -1;
                }
                break;
            }
            default:
                break;
            }
        }

        static void Overlay(Traits& into, const Traits& over) {
            if (over.solid >= 0)
                into.solid = over.solid;
            if (over.support >= 0)
                into.support = over.support;
            if (over.collision >= 0) {
                into.collision = over.collision;
                std::copy(over.box, over.box + 6, into.box);
            }
            if (over.transparent >= 0)
                into.transparent = over.transparent;
            if (over.replaceable >= 0)
                into.replaceable = over.replaceable;
            if (over.liquid >= 0)
                into.liquid = over.liquid;
            if (over.hasPlacement) {
                into.hasPlacement = true;
                into.placement = over.placement;
                into.denied = over.denied;
            }
        }

        Impl() {
            for (const char* const* line = kBlockTraitsLines; *line; line++) {
                std::string_view l = *line;
                if (l.starts_with('#')) {
                    const bool denied = l.starts_with("#!");
                    const auto parts = Split(l.substr(denied ? 2 : 1), '|');
                    PlacementList list;
                    if (parts.size() > 1 && !parts[1].empty()) {
                        if (denied) {
                            list.blocks = Split(parts[1], ',');
                        } else {
                            for (const std::string& group : Split(parts[1], ';')) {
                                const size_t colon = group.find(':');
                                std::vector<PlacementFace> faces;
                                for (const std::string& f : Split(group.substr(0, colon), ','))
                                    faces.push_back(FaceOf(f));
                                list.rules.emplace_back(std::move(faces), Split(group.substr(colon + 1), ','));
                            }
                        }
                    }
                    placementIndex[parts[0]] = static_cast<int16_t>(placements.size());
                    placements.push_back(std::move(list));
                    continue;
                }
            }
            for (const char* const* line = kBlockTraitsLines; *line; line++) {
                std::string_view l = *line;
                if (l.starts_with('#'))
                    continue;
                const size_t bar = l.find('|');
                std::vector<std::string> parts{std::string(l.substr(0, bar))};
                for (std::string& p : Split(bar == std::string_view::npos ? std::string_view() : l.substr(bar + 1), ';'))
                    parts.push_back(std::move(p));
                Def def;
                if (parts.size() > 1)
                    for (const std::string& t : Split(parts[1], ' '))
                        if (!t.empty())
                            ApplyTraits(def.base, t);
                for (size_t i = 2; i < parts.size(); i++) {
                    if (parts[i].find("=>") == std::string::npos)
                        continue;
                    const size_t arrow = parts[i].find("=>");
                    Parser p{std::string_view(parts[i]).substr(0, arrow)};
                    Traits t;
                    for (const std::string& tok : Split(parts[i].substr(arrow + 2), ' '))
                        if (!tok.empty())
                            ApplyTraits(t, tok);
                    def.perms.emplace_back(p.Or(), t);
                }
                defs.emplace(parts[0], std::move(def));
            }
        }

        uint16_t KindOf(const std::string& name) const {
            const auto [it, added] = kinds.try_emplace(name, static_cast<uint16_t>(kinds.size()));
            return it->second;
        }

        const BlockEntry* Build(BlockId id) const {
            auto e = std::make_unique<BlockEntry>();
            const uint16_t runtime = Blocks::Runtime(id);
            e->flooded = (id & kFloodedBit) != 0;
            e->name = BlockRegistry::Instance().Name(runtime);
            e->states = BlockRegistry::Instance().States(runtime);
            e->kind = KindOf(e->name);
            for (int t = 0; t < static_cast<int>(Tag::Count); t++)
                for (const char* member : TagMembers(static_cast<Tag>(t)))
                    if (e->name == member)
                        e->tags |= 1u << t;
            Traits traits;
            bool known = false;
            if (const auto it = defs.find(e->name); it != defs.end()) {
                known = true;
                traits = it->second.base;
                for (const auto& [cond, over] : it->second.perms)
                    if (cond->Eval(e->states))
                        Overlay(traits, over);
            }
            const SupportFaces faces = traits.support == 1 ? SupportFaces::Top : traits.support == 2 ? SupportFaces::All : SupportFaces::None;
            e->faceSupport = faces;
            e->bdsSolid = !known || traits.solid != 0;
            e->solid = e->bdsSolid;
            e->transparent = traits.transparent == 1;
            e->full = faces == SupportFaces::All;
            e->cube = e->full;
            e->vineSupport = faces == SupportFaces::All && e->bdsSolid;
            e->collisionCube = !known || (traits.solid != 0 && (traits.collision < 0 || traits.collision == 1 ||
                                                                 (traits.collision == 2 && traits.box[0] == 0 && traits.box[1] == 0 &&
                                                                  traits.box[2] == 0 && traits.box[3] == 1 && traits.box[4] == 1 &&
                                                                  traits.box[5] == 1)));
            e->topCentre = faces != SupportFaces::None;
            e->raisedTop = faces == SupportFaces::Top;
            e->liquid = e->name == "minecraft:water" || e->name == "minecraft:lava";
            e->replaceable = traits.replaceable == 1;
            switch (traits.liquid) {
            case kContainsWater:
                e->waterloggable = true;
                break;
            case kContainsBrokenWater:
                e->waterloggable = true;
                e->waterTouch = LiquidTouch::Broken;
                break;
            case kContainsBrokenWaterBrokenLava:
                e->waterloggable = true;
                e->waterTouch = LiquidTouch::Broken;
                e->lavaTouch = LiquidTouch::Broken;
                break;
            case kBrokenWater:
                e->waterTouch = LiquidTouch::Broken;
                break;
            case kBrokenLava:
                e->lavaTouch = LiquidTouch::Broken;
                break;
            case kBrokenWaterLava:
                e->waterTouch = LiquidTouch::Broken;
                e->lavaTouch = LiquidTouch::Broken;
                break;
            default:
                break;
            }
            for (const auto& [k, v] : e->states) {
                if (k == "upper_block_bit")
                    if (const bool* b = std::get_if<bool>(&v))
                        e->upper = *b ? 1 : 0;
                if (k == "sea_grass_type")
                    if (const std::string* s = std::get_if<std::string>(&v))
                        e->upper = *s == "double_top" ? 1 : *s == "double_bot" ? 0 : e->upper;
            }
            e->placement = traits.placement;
            e->denied = traits.denied;
            e->support = SupportOf(e->name, e->tags, traits.hasPlacement && traits.placement >= 0);
            const BlockEntry* out = e.get();
            owned.push_back(std::move(e));
            return out;
        }
    };

    Blocks::Blocks() : impl(new Impl()) {}

    const Blocks& Blocks::Get() {
        static const Blocks blocks;
        return blocks;
    }

    const BlockEntry& Blocks::Entry(BlockId id) const {
        if (const BlockEntry* e = impl->entries[id].load(std::memory_order_acquire))
            return *e;
        std::lock_guard lock(impl->mutex);
        if (const BlockEntry* e = impl->entries[id].load(std::memory_order_relaxed))
            return *e;
        const BlockEntry* built = impl->Build(id);
        impl->entries[id].store(built, std::memory_order_release);
        return *built;
    }

    BlockId Blocks::Id(std::string_view nameIn, const StateList& states) const {
        std::string name(nameIn);
        if (!name.contains(':'))
            name = "minecraft:" + name;
        BlockCatalog& catalog = BlockCatalog::Instance();
        if (!catalog.Knows(name))
            return kAirId;
        const uint16_t def = catalog.DefaultBlock(name);
        if (states.empty())
            return def;
        StateList full = BlockRegistry::Instance().States(def);
        for (const auto& [k, v] : states)
            for (auto& [fk, fv] : full)
                if (fk == k) {
                    fv = v;
                    break;
                }
        const uint16_t id = BlockRegistry::Instance().Intern(BlockRegistry::Instance().Name(def), full);
        return catalog.KnownHash(BlockRegistry::Instance().Hash(id)) ? id : def;
    }

    BlockId Blocks::IdWith(std::string_view name, std::initializer_list<std::pair<const char*, StateValue>> states) const {
        StateList list;
        for (const auto& [k, v] : states)
            list.emplace_back(k, v);
        return Id(name, list);
    }

    uint16_t Blocks::Kind(std::string_view name) const {
        std::string n(name);
        if (!n.contains(':'))
            n = "minecraft:" + n;
        std::lock_guard lock(impl->mutex);
        return impl->KindOf(n);
    }

    const StateValue* Blocks::State(BlockId id, std::string_view key) const {
        for (const auto& [k, v] : Entry(id).states)
            if (k == key)
                return &v;
        return nullptr;
    }

    bool Blocks::Flag(BlockId id, std::string_view key) const {
        const StateValue* v = State(id, key);
        const bool* b = v ? std::get_if<bool>(v) : nullptr;
        return b && *b;
    }

    std::optional<int> Blocks::Int(BlockId id, std::string_view key) const {
        const StateValue* v = State(id, key);
        if (const int32_t* i = v ? std::get_if<int32_t>(v) : nullptr)
            return *i;
        return std::nullopt;
    }

    const std::string* Blocks::Text(BlockId id, std::string_view key) const {
        const StateValue* v = State(id, key);
        return v ? std::get_if<std::string>(v) : nullptr;
    }

    BlockId Blocks::With(BlockId id, std::string_view key, const StateValue& value) const {
        const BlockEntry& e = Entry(id);
        StateList states = e.states;
        bool found = false;
        for (auto& [k, v] : states)
            if (k == key) {
                if (v == value)
                    return id;
                v = value;
                found = true;
            }
        if (!found)
            return id;
        const uint16_t next = BlockRegistry::Instance().Intern(e.name, states);
        if (!BlockCatalog::Instance().KnownHash(BlockRegistry::Instance().Hash(next)))
            return id;
        return static_cast<BlockId>(next | (id & kFloodedBit));
    }

    bool Blocks::PlacedOn(BlockId block, BlockId neighbour, PlacementFace face) const {
        const BlockEntry& e = Entry(block);
        if (e.placement < 0)
            return false;
        const std::string& name = NameOf(neighbour);
        for (const auto& [faces, blocks] : impl->placements[static_cast<size_t>(e.placement)].rules)
            if (std::find(faces.begin(), faces.end(), face) != faces.end() && std::find(blocks.begin(), blocks.end(), name) != blocks.end())
                return true;
        return false;
    }

    bool Blocks::DeniedOn(BlockId block, BlockId neighbour) const {
        const BlockEntry& e = Entry(block);
        if (e.denied < 0)
            return false;
        const auto& list = impl->placements[static_cast<size_t>(e.denied)].blocks;
        return std::find(list.begin(), list.end(), NameOf(neighbour)) != list.end();
    }

    // --- survival -----------------------------------------------------------------------------------

    Fluid FluidAt(BlockId block) {
        const BlockEntry& e = Blocks::Get().Entry(block);
        if (e.name == "minecraft:water" || e.flooded)
            return Fluid::Water;
        if (e.name == "minecraft:lava")
            return Fluid::Lava;
        return Fluid::None;
    }

    Direction CoralFanFacing(BlockId fan) {
        switch (Blocks::Get().Int(fan, "coral_direction").value_or(0)) {
        case 1:
            return Direction::East;
        case 2:
            return Direction::North;
        case 3:
            return Direction::South;
        default:
            return Direction::West;
        }
    }

    namespace {
        bool Sturdy(const BlockView& view, const IVec3& pos) { return Blocks::Get().Entry(view.Get(pos)).full; }
    }

    bool CanSurvive(const BlockView& view, BlockId block, const IVec3& pos) {
        const Blocks& blocks = Blocks::Get();
        const BlockEntry& entry = blocks.Entry(block);
        const BlockId below = view.Get(pos.Down());
        if (entry.upper == 1)
            return blocks.SameKind(below, block) && blocks.Entry(below).upper != 1;
        switch (entry.support) {
        case Support::SugarCane: {
            if (blocks.SameKind(below, block))
                return true;
            if (!blocks.PlacedOn(block, below, PlacementFace::Up))
                return false;
            for (Direction d : kHorizontal) {
                const BlockId n = view.Get(pos.Down() + Offset(d));
                if (FluidAt(n) == Fluid::Water || blocks.Is(n, Tag::SupportsSugarCaneAdjacently))
                    return true;
            }
            return false;
        }
        case Support::Cactus: {
            for (Direction d : kHorizontal) {
                const BlockId n = view.Get(pos + Offset(d));
                if (blocks.Entry(n).solid || FluidAt(n) == Fluid::Lava)
                    return false;
            }
            return (blocks.SameKind(below, block) || blocks.PlacedOn(block, below, PlacementFace::Up)) &&
                   FluidAt(view.Get(pos.Up())) == Fluid::None;
        }
        case Support::CactusFlower: {
            const std::string& n = blocks.NameOf(below);
            return n == "minecraft:cactus" || n == "minecraft:farmland" || blocks.Entry(below).full;
        }
        case Support::Mushroom: {
            const BlockEntry& b = blocks.Entry(below);
            return blocks.Is(below, Tag::Mud) || b.name == "minecraft:mycelium" || b.name == "minecraft:podzol" ||
                   (b.full && !blocks.Is(below, Tag::Leaves));
        }
        case Support::SturdyFloor:
            return Sturdy(view, pos.Down());
        case Support::Seagrass:
            return Sturdy(view, pos.Down()) && !blocks.DeniedOn(block, below);
        case Support::SolidFloor:
            return blocks.Entry(below).solid;
        case Support::Kelp:
            return (Sturdy(view, pos.Down()) || blocks.NameOf(below) == "minecraft:kelp") && !blocks.DeniedOn(block, below);
        case Support::Propagule:
            if (blocks.Flag(block, "hanging"))
                return blocks.PlacedOn(block, view.Get(pos.Up()), PlacementFace::Down);
            return blocks.PlacedOn(block, below, PlacementFace::Up);
        case Support::SnowLayer:
            if (blocks.DeniedOn(block, below))
                return false;
            return blocks.PlacedOn(block, below, PlacementFace::Up) || blocks.Entry(below).full ||
                   (blocks.NameOf(below) == "minecraft:snow_layer" && blocks.Int(below, "height") == 7);
        case Support::AnyFloor:
            return !blocks.IsAir(below);
        case Support::SporeBlossom:
            return Sturdy(view, pos.Up()) && FluidAt(view.Get(pos)) == Fluid::None;
        case Support::SturdyCeiling:
            return Sturdy(view, pos.Up());
        case Support::PaleHangingMoss:
            return Sturdy(view, pos.Up()) || blocks.SameKind(view.Get(pos.Up()), block);
        case Support::Waterlily:
            return blocks.PlacedOn(block, below, PlacementFace::Up) && FluidAt(view.Get(pos)) == Fluid::None;
        case Support::BigDripleaf:
            return blocks.PlacedOn(block, below, PlacementFace::Up) || blocks.NameOf(below) == "minecraft:big_dripleaf";
        case Support::WallCoral:
            return Sturdy(view, pos - Offset(CoralFanFacing(block)));
        case Support::Coral:
            return Sturdy(view, pos.Down());
        case Support::Placed:
            return blocks.PlacedOn(block, below, PlacementFace::Up);
        case Support::Anywhere:
            return true;
        }
        return true;
    }

    // --- pale moss carpet ---------------------------------------------------------------------------

    namespace {
        enum class Side : uint8_t { None, Low, Tall };
        struct Carpet {
            bool base = false;
            std::array<Side, 4> sides{};
            int Index() const {
                int index = base ? 1 : 0;
                for (Side s : sides)
                    index = index * 3 + static_cast<int>(s);
                return index;
            }
            static Carpet FromIndex(int index) {
                Carpet c;
                for (int i = 3; i >= 0; i--) {
                    c.sides[static_cast<size_t>(i)] = static_cast<Side>(index % 3);
                    index /= 3;
                }
                c.base = index == 1;
                return c;
            }
            bool HasFaces() const {
                return base || std::any_of(sides.begin(), sides.end(), [](Side s) { return s != Side::None; });
            }
        };
        const char* const kSideNames[3] = {"none", "short", "tall"};
        const char* const kSideStates[4] = {"pale_moss_carpet_side_north", "pale_moss_carpet_side_east", "pale_moss_carpet_side_south",
                                            "pale_moss_carpet_side_west"};
    }

    MossCarpet::MossCarpet() {
        const Blocks& blocks = Blocks::Get();
        for (int index = 0; index < 162; index++) {
            const Carpet c = Carpet::FromIndex(index);
            StateList states{{"upper_block_bit", StateValue(!c.base)}};
            for (int i = 0; i < 4; i++)
                states.emplace_back(kSideStates[i], StateValue(std::string(kSideNames[static_cast<int>(c.sides[static_cast<size_t>(i)])])));
            this->states.push_back(blocks.Id("minecraft:pale_moss_carpet", states));
        }
        kind = blocks.Kind("minecraft:pale_moss_carpet");
    }

    const MossCarpet& MossCarpet::Get() {
        static const MossCarpet carpet;
        return carpet;
    }

    int MossCarpet::Decode(BlockId state) const {
        for (size_t i = 0; i < states.size(); i++)
            if (states[i] == state)
                return static_cast<int>(i);
        return -1;
    }

    BlockId MossCarpet::UpdateShape(const BlockView& view, BlockId state, const IVec3& pos) const {
        if (!CanSurvive(view, state, pos))
            return kAirId;
        const int index = Decode(state);
        if (index < 0)
            return state;
        Carpet carpet = Carpet::FromIndex(index);
        const bool createSides = carpet.base;
        for (size_t i = 0; i < 4; i++) {
            const Direction direction = kHorizontal[i];
            const bool supported = Blocks::Get().Entry(view.Get(pos + Offset(direction))).full;
            Side side = supported ? (createSides ? Side::Low : carpet.sides[i]) : Side::None;
            if (side == Side::Low) {
                if (const int above = Decode(view.Get(pos.Up())); above >= 0) {
                    const Carpet a = Carpet::FromIndex(above);
                    if (a.sides[i] != Side::None && !a.base)
                        side = Side::Tall;
                }
                if (!carpet.base)
                    if (const int below = Decode(view.Get(pos.Down())); below >= 0 && Carpet::FromIndex(below).sides[i] == Side::None)
                        side = Side::None;
            }
            carpet.sides[i] = side;
        }
        return carpet.HasFaces() ? states[static_cast<size_t>(carpet.Index())] : kAirId;
    }

    // --- shape updates ------------------------------------------------------------------------------

    namespace {
        BlockId Broken(BlockId state, BlockId water) { return Blocks::Get().Entry(state).flooded ? water : kAirId; }

        bool VineHasFace(BlockId vine, Direction face) {
            const int bits = Blocks::Get().Int(vine, "vine_direction_bits").value_or(0);
            if (bits == 0)
                return face == Direction::Up;
            return face != Direction::Up && (bits & VineBit(face)) != 0;
        }
    }

    BlockId Shapes::Update(const BlockView& view, const IVec3& pos, BlockId state, Direction direction) const {
        const Blocks& blocks = Blocks::Get();
        if (blocks.IsAir(state) || blocks.Entry(state).liquid)
            return state;
        const BlockId neighbour = view.Get(pos + Offset(direction));
        const MossCarpet& carpet = MossCarpet::Get();
        if (blocks.IsKind(state, carpet.kind))
            return carpet.UpdateShape(view, state, pos);
        if (const int8_t upper = blocks.Entry(state).upper; upper >= 0) {
            if (doublePlantsKeepOrphans)
                return state;
            const bool vertical = direction == Direction::Up || direction == Direction::Down;
            if (vertical && (upper == 1) != (direction == Direction::Up)) {
                const bool matching = blocks.SameKind(neighbour, state) && blocks.Entry(neighbour).upper != upper;
                return matching ? state : Broken(state, water);
            }
            return CanSurvive(view, state, pos) ? state : Broken(state, water);
        }
        const std::string& name = blocks.NameOf(state);
        if (name == "minecraft:vine") {
            if (direction == Direction::Down)
                return state;
            const BlockId above = view.Get(pos.Up());
            static constexpr Direction kCandidates[5] = {Direction::Up, Direction::North, Direction::East, Direction::South, Direction::West};
            for (Direction face : kCandidates) {
                if (!VineHasFace(state, face))
                    continue;
                const bool attached = blocks.Entry(view.Get(pos + Offset(face))).full;
                if (!(attached || (face != Direction::Up && blocks.SameKind(above, state) && VineHasFace(above, face))))
                    return kAirId;
            }
            return state;
        }
        if (name == "minecraft:glow_lichen" || name == "minecraft:sculk_vein") {
            const int bits = blocks.Int(state, "multi_face_direction_bits").value_or(0);
            const int bit = MultifaceBit(direction);
            if ((bits & bit) == 0 || blocks.Entry(neighbour).full)
                return state;
            const int remaining = bits & ~bit;
            return remaining == 0 ? kAirId : blocks.With(state, "multi_face_direction_bits", StateValue(remaining));
        }
        if (name == "minecraft:cocoa") {
            const Direction facing = FromLegacyDirection(blocks.Int(state, "direction").value_or(0));
            return facing == direction && !blocks.Is(neighbour, Tag::Logs) ? kAirId : state;
        }
        if (CanSurvive(view, state, pos) ||
            (packedIceHoldsSnow && name == "minecraft:snow_layer" && blocks.NameOf(view.Get(pos.Down())) == "minecraft:packed_ice"))
            return state;
        return Broken(state, water);
    }

    BlockId Shapes::UpdateFromNeighbours(const BlockView& view, const IVec3& pos) const {
        BlockId state = view.Get(pos);
        for (Direction d : {Direction::West, Direction::East, Direction::North, Direction::South, Direction::Down, Direction::Up})
            state = Update(view, pos, state, d);
        return state;
    }
}
