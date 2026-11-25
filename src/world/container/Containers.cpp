#include "server/VanillaPack.hpp"
#include "server/Server.hpp"
#include "common/Log.hpp"
#include "world/inventory/ItemNbt.hpp"

#include "world/crafting/Recipes.hpp"
#include "world/level/block/BlockCatalog.hpp"
#include "world/level/block/Blocks.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace perlite {
    namespace {
        int64_t Key(int x, int y, int z, int dimension) {
            return (static_cast<int64_t>(x) & 0x3FFFFFF) << 38 | (static_cast<int64_t>(z) & 0x3FFFFFF) << 12 |
                   static_cast<int64_t>(dimension & 3) << 10 | static_cast<int64_t>((y + 512) & 1023);
        }

        std::string Bare(const std::string& n) { return n.starts_with("minecraft:") ? n.substr(10) : n; }

        proto::ContainerSlotType SlotType(int kind) {
            switch (kind) {
            case 1:
                return proto::ContainerSlotType::Barrel;
            case 2:
                return proto::ContainerSlotType::FurnaceIngredient;
            case 3:
                return proto::ContainerSlotType::BlastFurnaceIngredient;
            case 4:
                return proto::ContainerSlotType::SmokerIngredient;
            case 13:
                return proto::ContainerSlotType::BrewingInput;
            default:
                return proto::ContainerSlotType::Container;
            }
        }

        proto::WindowType WindowOf(int kind) {
            switch (kind) {
            case 2:
                return proto::WindowType::Furnace;
            case 3:
                return proto::WindowType::BlastFurnace;
            case 4:
                return proto::WindowType::Smoker;
            case 5:
                return proto::WindowType::Workbench;
            case 6:
                return proto::WindowType::Stonecutter;
            case 7:
                return proto::WindowType::SmithingTable;
            case 8:
                return proto::WindowType::Grindstone;
            case 9:
                return proto::WindowType::Loom;
            case 10:
                return proto::WindowType::Cartography;
            case 11:
                return proto::WindowType::Anvil;
            case 12:
                return proto::WindowType::Enchantment;
            case 13:
                return proto::WindowType::BrewingStand;
            default:
                return proto::WindowType::Container;
            }
        }

        std::string ItemName(int16_t id) {
            const CatalogItem* item = BlockCatalog::Instance().ItemById(id);
            return item ? item->name : std::string();
        }
    }

    int Server::ContainerKindOf(const std::string& blockName) {
        const std::string n = Bare(blockName);
        if (n == "chest" || n == "trapped_chest")
            return 0;
        if (n == "barrel")
            return 1;
        if (n == "furnace" || n == "lit_furnace")
            return 2;
        if (n == "blast_furnace" || n == "lit_blast_furnace")
            return 3;
        if (n == "smoker" || n == "lit_smoker")
            return 4;
        if (n == "crafting_table")
            return 5;
        // The stations whose slots are the player's own ui slots (nothing kept in the block).
        if (n == "stonecutter_block" || n == "stonecutter")
            return 6;
        if (n == "smithing_table")
            return 7;
        if (n == "grindstone")
            return 8;
        if (n == "loom")
            return 9;
        if (n == "cartography_table")
            return 10;
        if (n == "anvil" || n == "chipped_anvil" || n == "damaged_anvil")
            return 11;
        if (n == "enchanting_table")
            return 12;
        if (n == "brewing_stand")
            return 13;
        return -1;
    }

    // Wooden doors, trapdoors and fence gates open and close by hand (iron ones only by redstone). A door
    // keeps the same open state in both halves; a gate opened from behind swings away from the player.
    bool Server::ToggleOpenable(Session& by, int dim, int x, int y, int z) {
        ChunkManager& world = World(dim);
        const uint16_t block = world.GetBlock(x, y, z);
        const std::string name = BlockRegistry::Instance().Name(block);
        const bool door = name.ends_with("_door") && !name.ends_with("trapdoor");
        const bool trapdoor = name.ends_with("trapdoor");
        const bool gate = name.ends_with("fence_gate");
        if (!door && !trapdoor && !gate)
            return false;
        if (name == "minecraft:iron_door" || name == "minecraft:iron_trapdoor")
            return false;
        StateList states = BlockRegistry::Instance().States(block);
        bool open = false;
        for (const auto& [k, v] : states)
            if (k == "open_bit")
                if (const bool* b = std::get_if<bool>(&v))
                    open = !*b;
        const auto withOpen = [&](uint16_t id, bool value, const std::string* facing) {
            StateList st = BlockRegistry::Instance().States(id);
            for (auto& [k, v] : st) {
                if (k == "open_bit")
                    v = value;
                if (facing && k == "minecraft:cardinal_direction")
                    v = *facing;
            }
            const uint16_t next = BlockRegistry::Instance().Intern(BlockRegistry::Instance().Name(id), st);
            return BlockCatalog::Instance().KnownHash(BlockRegistry::Instance().Hash(next)) ? next : id;
        };
        std::string facing;
        if (gate && open) {
            // Swing away from the player: a gate seen from its back turns round first.
            const float yaw = by.Yaw();
            const int look = static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3; // 0 south, 1 west, 2 north, 3 east
            static const char* const kLook[4] = {"south", "west", "north", "east"};
            static const char* const kOpposite[4] = {"north", "east", "south", "west"};
            for (const auto& [k, v] : states)
                if (k == "minecraft:cardinal_direction")
                    if (const std::string* f = std::get_if<std::string>(&v); f && *f == kOpposite[look])
                        facing = kLook[look];
        }
        ApplyBlock(dim, x, y, z, withOpen(block, open, facing.empty() ? nullptr : &facing));
        if (door) {
            for (int dy : {-1, 1}) {
                const uint16_t other = world.GetBlock(x, y + dy, z);
                if (BlockRegistry::Instance().Name(other) == name)
                    ApplyBlock(dim, x, y + dy, z, withOpen(other, open, nullptr));
            }
        }
        const char* sound = door ? (open ? "DoorOpen" : "DoorClose") : trapdoor ? (open ? "TrapdoorOpen" : "TrapdoorClose")
                                                                         : (open ? "FenceGateOpen" : "FenceGateClose");
        const int32_t hash = BlockRegistry::Instance().Hash(block);
        for (Session* s : PlayersIn(dim))
            s->PlaySound(sound, x + 0.5, y + 0.5, z + 0.5, hash);
        return true;
    }

    bool Server::UseBlock(Session& by, int x, int y, int z) {
        const int dim = by.DimensionId();
        ChunkManager& world = World(dim);
        if (by.Spectator() || by.Visitor() || !by.InReach(x, y, z) || !world.Peek({x >> 4, z >> 4}))
            return false;
        const std::string name = BlockRegistry::Instance().Name(world.GetBlock(x, y, z));
        if (name == "minecraft:campfire" || name == "minecraft:soul_campfire") {
            // Food goes on a campfire's four spots and cooks in thirty seconds.
            ItemStack& held = by.Inventory().HeldRef();
            if (held.Empty() || !Recipes::Instance().Smelt(ItemName(held.networkId), 3))
                return false;
            auto [it, fresh] = containers.try_emplace(Key(x, y, z, dim));
            BlockContainer& c = it->second;
            if (fresh) {
                c.kind = 14;
                c.dimension = dim;
                c.x = x;
                c.y = y;
                c.z = z;
                c.slots.assign(4, ItemStack{});
            }
            for (size_t i = 0; i < 4; i++)
                if (c.slots[i].Empty()) {
                    c.slots[i] = held;
                    c.slots[i].count = 1;
                    c.timers[i] = 0;
                    if (!by.Creative()) {
                        by.Inventory().TakeHeld(1);
                        by.SendSlot(by.Inventory().selected);
                    }
                    return true;
                }
            return false;
        }
        if (name == "minecraft:bed" || name.ends_with("_bed")) {
            by.StartSleeping(x, y, z);
            return true;
        }
        if (ToggleOpenable(by, dim, x, y, z))
            return true;
        const int kind = ContainerKindOf(name);
        if (kind < 0)
            return false;
        if (HookContainerOpen(by, x, y, z, name))
            return true;
        CloseContainer(by);
        static int windowCounter = 0;
        const int window = 1 + (windowCounter++ % 99);
        if (kind >= 5 && kind != 13) {
            by.Inventory().containerKind = kind == 12 ? 12 : -1;
            by.Inventory().container.clear();
            if (kind == 12) {
                // Bookshelves around the table with air between them and it (the game's rule).
                int shelves = 0;
                const uint16_t shelf = BlockCatalog::Instance().DefaultBlock("minecraft:bookshelf");
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++)
                        if (std::abs(dx) == 2 || std::abs(dz) == 2)
                            for (int dy = 0; dy <= 1; dy++)
                                if (world.GetBlock(x + dx, y + dy, z + dz) == shelf &&
                                    world.GetBlock(x + dx / 2, y + dy, z + dz / 2) == BlockRegistry::kAir)
                                    shelves++;
                by.Inventory().bookshelves = shelves;
                by.Inventory().enchantOffers.clear();
            }
            by.OpenWindow(window, WindowOf(kind), x, y, z);
            return true;
        }
        auto [it, fresh] = containers.try_emplace(Key(x, y, z, dim));
        BlockContainer& c = it->second;
        if (fresh) {
            c.kind = kind;
            c.dimension = dim;
            c.x = x;
            c.y = y;
            c.z = z;
            c.slots.assign(kind == 13 ? 5 : kind >= 2 ? 3 : 27, ItemStack{});
        }
        by.Inventory().containerKind = kind;
        by.Inventory().container = c.slots;
        for (ItemStack& slot : c.slots)
            if (!slot.Empty() && slot.stackId == 0)
                slot.stackId = by.Inventory().NewStackId(); // loot rolled at generation has none yet
        by.Inventory().container = c.slots;
        by.openContainer = Key(x, y, z, dim);
        by.OpenWindow(window, WindowOf(kind), x, y, z);
        ContainerLid(dim, x, y, z, kind, true);
        by.SendWindowContents(SlotType(kind), c.slots);
        if (kind >= 2) {
            by.SendWindowData(0, c.cook);
            by.SendWindowData(1, c.burn);
            by.SendWindowData(2, c.burnMax);
        }
        return true;
    }

    void Server::SyncContainer(Session& by) {
        const auto it = containers.find(by.openContainer);
        if (it == containers.end())
            return;
        it->second.slots = by.Inventory().container;
        for (Session* s : InGamePlayers())
            if (s != &by && s->openContainer == by.openContainer) {
                s->Inventory().container = it->second.slots;
                s->SendWindowContents(SlotType(it->second.kind), it->second.slots);
            }
    }

    void Server::CloseContainer(Session& by) {
        PlayerInventory& inv = by.Inventory();
        bool gave = false;
        for (ItemStack& s : inv.craft)
            if (!s.Empty()) {
                const int left = inv.Give(s);
                if (left > 0) {
                    ItemStack rest = s;
                    rest.count = static_cast<uint16_t>(left);
                    DropFromPlayer(by, rest);
                }
                s = ItemStack{};
                gave = true;
            }
        const int64_t was = by.openContainer;
        if (inv.containerKind >= 0) {
            SyncContainer(by);
            inv.containerKind = -1;
            inv.container.clear();
        }
        by.openContainer = INT64_MIN;
        if (const auto it = containers.find(was); it != containers.end())
            ContainerLid(it->second.dimension, it->second.x, it->second.y, it->second.z, it->second.kind, false);
        by.ClearWindow();
        if (gave)
            by.SendInventory();
    }

    // A chest's lid opens for its first viewer and shuts after its last (the block event carries
    // how many are looking); a barrel shows it through its open_bit. Both with the game's sounds.
    void Server::ContainerLid(int dimension, int x, int y, int z, int kind, bool opening) {
        if (kind != 0 && kind != 1)
            return;
        const int64_t key = Key(x, y, z, dimension);
        int viewers = 0;
        for (Session* s : InGamePlayers())
            if (s->openContainer == key)
                viewers++;
        if (opening ? viewers != 1 : viewers != 0)
            return; // only the first opening and the last closing move the lid
        const std::vector<Session*> near = PlayersIn(dimension);
        if (kind == 0) {
            for (Session* s : near) {
                s->SendBlockEvent(x, y, z, 1, viewers);
                s->PlaySound(opening ? "ChestOpen" : "ChestClosed", x + 0.5, y + 0.5, z + 0.5);
            }
            return;
        }
        const uint16_t current = BlockAt(dimension, x, y, z);
        auto states = BlockRegistry::Instance().States(current);
        bool changed = false;
        for (auto& st : states)
            if (st.first == "open_bit") {
                if (std::holds_alternative<bool>(st.second))
                    st.second = opening;
                else
                    st.second = static_cast<int32_t>(opening ? 1 : 0);
                changed = true;
            }
        if (changed)
            ApplyBlock(dimension, x, y, z, BlockRegistry::Instance().Intern(BlockRegistry::Instance().Name(current), states));
        for (Session* s : near)
            s->PlaySound(opening ? "BarrelOpen" : "BarrelClose", x + 0.5, y + 0.5, z + 0.5);
    }

    void Server::DropContainer(int dimension, int x, int y, int z) {
        const auto it = containers.find(Key(x, y, z, dimension));
        if (it == containers.end())
            return;
        for (Session* s : InGamePlayers())
            if (s->openContainer == it->first) {
                s->Inventory().containerKind = -1;
                s->Inventory().container.clear();
                s->openContainer = INT64_MIN;
                s->CloseWindowFromServer();
            }
        for (const ItemStack& stack : it->second.slots)
            if (!stack.Empty())
                SpawnItem(stack, x + 0.5, y + 0.5, z + 0.5, 0.0, 0.15, 0.0, 10, dimension);
        containers.erase(it);
    }

    void Server::PushContainer(BlockContainer& c, bool slotsChanged) {
        const int64_t key = Key(c.x, c.y, c.z, c.dimension);
        for (Session* s : InGamePlayers())
            if (s->openContainer == key) {
                if (slotsChanged) {
                    s->Inventory().container = c.slots;
                    s->SendWindowContents(SlotType(c.kind), c.slots);
                }
                s->SendWindowData(0, c.cook);
                s->SendWindowData(1, c.burn);
                s->SendWindowData(2, c.burnMax);
            }
    }

    void Server::TickContainers() {
        for (auto& [key, c] : containers) {
            if (c.kind == 13) {
                TickBrewing(c);
                continue;
            }
            if (c.kind == 14) {
                const std::string block = BlockRegistry::Instance().Name(BlockAt(c.dimension, c.x, c.y, c.z));
                if (!block.contains("campfire")) {
                    for (ItemStack& s : c.slots)
                        if (!s.Empty())
                            SpawnItem(s, c.x + 0.5, c.y + 0.5, c.z + 0.5, 0, 0.1, 0, 10, c.dimension);
                    c.slots.assign(4, ItemStack{});
                    continue;
                }
                const bool lit = BlockRegistry::Instance().Name(BlockAt(c.dimension, c.x, c.y, c.z)) == block &&
                                 BlockCatalog::Instance().StateText(BlockAt(c.dimension, c.x, c.y, c.z)).find("extinguished=1") == std::string::npos;
                for (size_t i = 0; i < 4; i++) {
                    ItemStack& s = c.slots[i];
                    if (s.Empty() || !lit || ++c.timers[i] < 600)
                        continue;
                    if (const SmeltRecipe* r = Recipes::Instance().Smelt(ItemName(s.networkId), 3)) {
                        ItemStack cooked;
                        cooked.networkId = r->outputId;
                        cooked.count = 1;
                        SpawnItem(cooked, c.x + 0.5, c.y + 1.0, c.z + 0.5, 0, 0.1, 0, 10, c.dimension);
                    }
                    s = ItemStack{};
                    c.timers[i] = 0;
                }
                continue;
            }
            if (c.kind < 2 || c.kind > 4)
                continue;
            const int station = c.kind - 2;
            const int cookTime = station == 0 ? 200 : 100;
            ItemStack& in = c.slots[0];
            ItemStack& fuel = c.slots[1];
            ItemStack& out = c.slots[2];
            const SmeltRecipe* recipe = in.Empty() ? nullptr : Recipes::Instance().Smelt(ItemName(in.networkId), station);
            const bool room = recipe && (out.Empty() || (out.networkId == recipe->outputId &&
                                                          out.count + recipe->outputCount <= MaxStack(out.networkId)));
            bool changed = false;
            const bool wasLit = c.burn > 0;
            if (c.burn > 0)
                c.burn--;
            if (c.burn == 0 && room && !fuel.Empty()) {
                const std::string fuelName = ItemName(fuel.networkId);
                const int ticks = Recipes::FuelTicks(fuelName) / (station == 0 ? 1 : 2);
                if (ticks > 0) {
                    c.burn = c.burnMax = ticks;
                    if (Bare(fuelName) == "lava_bucket") {
                        if (const CatalogItem* bucket = BlockCatalog::Instance().ItemByName("minecraft:bucket"))
                            fuel.networkId = bucket->networkId;
                    } else if (--fuel.count == 0) {
                        fuel = ItemStack{};
                    }
                    changed = true;
                }
            }
            if (c.burn > 0 && room) {
                if (++c.cook >= cookTime) {
                    c.cook = 0;
                    if (out.Empty()) {
                        out.networkId = recipe->outputId;
                        out.count = static_cast<uint16_t>(recipe->outputCount);
                    } else {
                        out.count = static_cast<uint16_t>(out.count + recipe->outputCount);
                    }
                    if (--in.count == 0)
                        in = ItemStack{};
                    changed = true;
                }
            } else if (c.cook > 0) {
                c.cook = std::max(0, c.cook - 2);
            }
            const bool lit = c.burn > 0;
            ChunkManager* world = LoadedWorld(c.dimension);
            if (lit != wasLit && world && world->Peek({c.x >> 4, c.z >> 4})) {
                const uint16_t block = world->GetBlock(c.x, c.y, c.z);
                std::string text = BlockCatalog::Instance().StateText(block);
                const std::string name = text.substr(0, text.find('['));
                const std::string bare = Bare(name);
                std::string target = bare;
                if (lit && !bare.starts_with("lit_"))
                    target = "lit_" + bare;
                if (!lit && bare.starts_with("lit_"))
                    target = bare.substr(4);
                if (target != bare)
                    ApplyBlock(c.dimension, c.x, c.y, c.z, BlockCatalog::Instance().Resolve("minecraft:" + target + text.substr(name.size())));
            }
            if (changed)
                for (ItemStack& s : c.slots)
                    if (!s.Empty())
                        s.stackId = static_cast<int32_t>(tick & 0x7FFFFFFF) + static_cast<int32_t>(&s - c.slots.data());
            if (changed || (lit && tick % 5 == 0) || lit != wasLit)
                PushContainer(c, changed);
        }
    }

    namespace {
        const char* BlockEntityId(int kind) {
            switch (kind) {
            case 0:
                return "Chest";
            case 1:
                return "Barrel";
            case 2:
                return "Furnace";
            case 3:
                return "BlastFurnace";
            case 4:
                return "Smoker";
            default:
                return "";
            }
        }

        int KindOfBlockEntity(const std::string& id) {
            if (id == "Chest")
                return 0;
            if (id == "Barrel")
                return 1;
            if (id == "Furnace")
                return 2;
            if (id == "BlastFurnace")
                return 3;
            if (id == "Smoker")
                return 4;
            return -1;
        }
    }

    void Server::SaveContainers() {
        if (!storage)
            return;
        std::map<std::tuple<int, int, int>, std::vector<nbt::Value>> byChunk;
        for (const auto& key : blockEntityChunks)
            byChunk[key];
        for (const auto& [key, c] : containers) {
            const char* id = BlockEntityId(c.kind);
            if (!*id)
                continue;
            nbt::Value e = nbt::Value::Make(nbt::Compound);
            e.SetString("id", id);
            e.SetInt("x", c.x);
            e.SetInt("y", c.y);
            e.SetInt("z", c.z);
            e.SetByte("isMovable", 1);
            e.Set("Items", ItemListToNbt(c.slots));
            if (c.kind >= 2) {
                e.SetShort("BurnTime", static_cast<int16_t>(c.burn));
                e.SetShort("BurnDuration", static_cast<int16_t>(c.burnMax));
                e.SetShort("CookTime", static_cast<int16_t>(c.cook));
            }
            byChunk[{c.dimension, c.x >> 4, c.z >> 4}].push_back(std::move(e));
        }
        blockEntityChunks.clear();
        for (auto& [key, list] : byChunk) {
            const auto [dim, cx, cz] = key;
            storage->SaveBlockEntities(dim, {cx, cz}, list);
            if (!list.empty())
                blockEntityChunks.insert(key);
        }
    }

    void Server::LoadContainers(int dimension, ChunkPos pos) {
        if (!storage)
            return;
        for (const nbt::Value& e : storage->LoadBlockEntities(dimension, pos)) {
            const int kind = KindOfBlockEntity(e.String("id"));
            if (kind < 0)
                continue;
            const int x = static_cast<int>(e.Int("x")), y = static_cast<int>(e.Int("y")),
                      z = static_cast<int>(e.Int("z"));
            auto [it, fresh] = containers.try_emplace(Key(x, y, z, dimension));
            if (!fresh)
                continue;
            BlockContainer& c = it->second;
            c.kind = kind;
            c.dimension = dimension;
            c.x = x;
            c.y = y;
            c.z = z;
            c.slots.assign(kind == 13 ? 5 : kind >= 2 ? 3 : 27, ItemStack{});
            ItemListFromNbt(e.Find("Items"), nullptr, c.slots);
            c.burn = static_cast<int>(e.Int("BurnTime"));
            c.burnMax = static_cast<int>(e.Int("BurnDuration"));
            c.cook = static_cast<int>(e.Int("CookTime"));
            c.lit = c.burn > 0;
            blockEntityChunks.insert({dimension, pos.x, pos.z});
        }
    }
}

#include "world/actor/ActorManager.hpp"
#include "world/loot/Loot.hpp"
#include "world/level/chunk/ChunkManager.hpp"
#include "world/level/structure/Templates.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlite {
    namespace {
        // Chest loot tables the built-in data lacks come from the structure pack's loot_tables
        // folder (the same Bedrock pack the templates come from).
        bool EnsureLootTable(const ServerConfig& config, const std::string& key) {
            if (LootTables::Instance().Has(key))
                return true;
            // Loot tables come from the structure pack when one is installed, else from the vanilla behavior pack.
            static const std::vector<std::string> roots = [&] {
                std::vector<std::string> out;
                if (const auto store = structure::TemplateStore::Find(config.structurePack, config.dataDir))
                    out.push_back(std::filesystem::path(store->Directory()).parent_path().string());
                const std::string vanilla = vanillapack::Ensure(config.vanillaPackDir.empty() ? config.dataDir : config.vanillaPackDir, false);
                if (!vanilla.empty())
                    out.push_back(vanilla);
                return out;
            }();
            std::ifstream in;
            for (const std::string& root : roots) {
                in = std::ifstream(std::filesystem::path(root) / "loot_tables" / (key + ".json"));
                if (in)
                    break;
            }
            if (!in)
                return false;
            std::stringstream text;
            text << in.rdbuf();
            std::string error;
            return LootTables::Instance().Override(key, text.str(), error);
        }
    }

    // A newly generated chunk's structure leftovers: chests are filled from their loot tables (the
    // items scattered over the slots, as the game does) and the structure's mobs appear.
    void Server::ConsumeGenerated(int dimension, ChunkPos pos) {
        ChunkManager* world = LoadedWorld(dimension);
        if (!world)
            return;
        const auto entry = world->Peek(pos);
        if (!entry)
            return;
        if (entry->column.generatedNow) {
            entry->column.generatedNow = false;
            ContentOnNewChunk(dimension, pos, entry->column);
        }
        std::vector<GeneratedExtra> extras = std::move(entry->column.generated);
        entry->column.generated.clear();
        SettleSpawn(dimension, pos);
        if (bonusPending && dimension == kOverworld && pos.x == (spawnX >> 4) && pos.z == (spawnZ >> 4)) {
            bonusPending = false;
            PlaceBonusChest(pos, extras);
        }
        if (extras.empty())
            return;
        for (const GeneratedExtra& e : extras) {
            if (e.kind == GeneratedExtra::Loot) {
                const int kind = ContainerKindOf(BlockRegistry::Instance().Name(BlockAt(dimension, e.x, e.y, e.z)));
                if (kind < 0 || kind > 1)
                    continue;
                auto [it, fresh] = containers.try_emplace(Key(e.x, e.y, e.z, dimension));
                BlockContainer& c = it->second;
                c.kind = kind;
                c.dimension = dimension;
                c.x = e.x;
                c.y = e.y;
                c.z = e.z;
                c.slots.assign(27, ItemStack{});
                LootContext ctx;
                ctx.rng = &worldRng;
                std::vector<int> free;
                for (int i = 0; i < 27; i++)
                    free.push_back(i);
                std::shuffle(free.begin(), free.end(), worldRng);
                if (!EnsureLootTable(config, e.text))
                    continue;
                for (const LootDrop& d : LootTables::Instance().Roll(e.text, ctx)) {
                    const CatalogItem* item = BlockCatalog::Instance().ItemByName(d.item);
                    if (!item || free.empty())
                        continue;
                    ItemStack stack;
                    stack.networkId = item->networkId;
                    stack.count = static_cast<uint16_t>(std::clamp(d.count, 1, 64));
                    c.slots[static_cast<size_t>(free.back())] = stack;
                    free.pop_back();
                }
                blockEntityChunks.insert({dimension, pos.x, pos.z});
            } else {
                std::string id = e.text;
                if (id == "minecraft:villager")
                    id = "minecraft:villager_v2";
                if (id == "minecraft:zombie_villager")
                    id = "minecraft:zombie_villager_v2";
                MobsIn(dimension).Spawn(id, e.x + 0.5, e.y, e.z + 0.5, true);
            }
        }
    }

    // The Bonus Chest: a chest of starting items on the ground somewhere in the spawn chunk, with a torch
    // on each open side, as the game's bonus chest feature places it. Its items roll from the vanilla
    // chests/spawn_bonus_chest loot table when the chest is filled below.
    void Server::PlaceBonusChest(ChunkPos pos, std::vector<GeneratedExtra>& extras) {
        ChunkManager& world = World(kOverworld);
        const DimensionRange range = RangeOf(kOverworld);
        const BlockRegistry& registry = BlockRegistry::Instance();
        const auto named = [&](uint16_t id, std::string_view part) { return registry.Name(id).contains(part); };
        std::vector<int> columns(256);
        for (int i = 0; i < 256; i++)
            columns[static_cast<size_t>(i)] = i;
        std::shuffle(columns.begin(), columns.end(), worldRng);
        for (const int c : columns) {
            const int x = pos.x * 16 + (c & 15), z = pos.z * 16 + (c >> 4);
            int ground = range.minY - 1;
            for (int y = range.MaxY(); y >= range.minY; y--) {
                const uint16_t id = world.GetBlock(x, y, z);
                if (id == BlockRegistry::kAir || named(id, "leaves"))
                    continue;
                if (named(id, "water") || named(id, "lava"))
                    break;
                if (BlockSolid(id)) {
                    ground = y;
                    break;
                }
            }
            const int y = ground + 1;
            if (ground < range.minY || y > range.MaxY() || world.GetBlock(x, y, z) != BlockRegistry::kAir)
                continue;
            const uint16_t chest = BlockCatalog::Instance().DefaultBlock("minecraft:chest");
            const uint16_t torch = BlockCatalog::Instance().DefaultBlock("minecraft:torch");
            SetBlockIn(kOverworld, x, y, z, chest);
            static const int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& side : kSides) {
                const int tx = x + side[0], tz = z + side[1];
                if (!world.Peek({tx >> 4, tz >> 4}))
                    continue;
                if (world.GetBlock(tx, y, tz) == BlockRegistry::kAir && BlockSolid(world.GetBlock(tx, y - 1, tz)))
                    SetBlockIn(kOverworld, tx, y, tz, torch);
            }
            extras.push_back({GeneratedExtra::Loot, x, y, z, "chests/spawn_bonus_chest"});
            LogInfo("Bonus chest at " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
            return;
        }
    }

    // A brewing stand: blaze powder gives twenty brews; a brew takes twenty seconds and turns each
    // bottle the ingredient fits into the next potion.
    void Server::TickBrewing(BlockContainer& c) {
        if (c.slots.size() != 5)
            return;
        ItemStack& ingredient = c.slots[0];
        ItemStack& fuel = c.slots[4];
        const auto brewsAny = [&] {
            if (ingredient.Empty())
                return false;
            for (int i = 1; i <= 3; i++) {
                int16_t item;
                uint16_t meta;
                if (!c.slots[static_cast<size_t>(i)].Empty() &&
                    BrewResult(c.slots[static_cast<size_t>(i)].networkId, c.slots[static_cast<size_t>(i)].meta, ingredient.networkId, item, meta))
                    return true;
            }
            return false;
        };
        bool changed = false;
        if (c.burn <= 0 && !fuel.Empty() && ItemName(fuel.networkId) == "minecraft:blaze_powder" && brewsAny()) {
            c.burn = c.burnMax = 20;
            if (--fuel.count == 0)
                fuel = ItemStack{};
            changed = true;
        }
        if (brewsAny() && c.burn > 0) {
            if (c.cook == 0)
                c.cook = 400;
            if (--c.cook == 0) {
                for (int i = 1; i <= 3; i++) {
                    ItemStack& bottle = c.slots[static_cast<size_t>(i)];
                    int16_t item;
                    uint16_t meta;
                    if (!bottle.Empty() && BrewResult(bottle.networkId, bottle.meta, ingredient.networkId, item, meta)) {
                        bottle.networkId = item;
                        bottle.meta = meta;
                    }
                }
                const std::string used = ItemName(ingredient.networkId);
                if (--ingredient.count == 0)
                    ingredient = ItemStack{};
                c.burn--;
                changed = true;
                (void)used;
            }
        } else if (c.cook > 0) {
            c.cook = 0;
            changed = true;
        }
        for (Session* s : PlayersIn(c.dimension)) {
            if (s->openContainer != Key(c.x, c.y, c.z, c.dimension))
                continue;
            if (changed) {
                s->Inventory().container = c.slots;
                s->SendWindowContents(proto::ContainerSlotType::BrewingInput, c.slots);
            }
            s->SendWindowData(0, c.cook);
            s->SendWindowData(1, c.burn);
            s->SendWindowData(2, c.burnMax);
        }
    }
}
