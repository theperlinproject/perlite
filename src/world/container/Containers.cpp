#include "server/Server.hpp"
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
        return -1;
    }

    bool Server::UseBlock(Session& by, int x, int y, int z) {
        const int dim = by.DimensionId();
        ChunkManager& world = World(dim);
        if (by.Spectator() || by.Visitor() || !by.InReach(x, y, z) || !world.Peek({x >> 4, z >> 4}))
            return false;
        const std::string name = BlockRegistry::Instance().Name(world.GetBlock(x, y, z));
        if (name == "minecraft:bed" || name.ends_with("_bed")) {
            by.StartSleeping(x, y, z);
            return true;
        }
        const int kind = ContainerKindOf(name);
        if (kind < 0)
            return false;
        if (HookContainerOpen(by, x, y, z, name))
            return true;
        CloseContainer(by);
        static int windowCounter = 0;
        const int window = 1 + (windowCounter++ % 99);
        if (kind >= 5) {
            by.Inventory().containerKind = -1;
            by.Inventory().container.clear();
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
            c.slots.assign(kind >= 2 ? 3 : 27, ItemStack{});
        }
        by.Inventory().containerKind = kind;
        by.Inventory().container = c.slots;
        by.openContainer = Key(x, y, z, dim);
        by.OpenWindow(window, WindowOf(kind), x, y, z);
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
        if (inv.containerKind >= 0) {
            SyncContainer(by);
            inv.containerKind = -1;
            inv.container.clear();
        }
        by.openContainer = INT64_MIN;
        by.ClearWindow();
        if (gave)
            by.SendInventory();
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
            if (c.kind < 2)
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
            c.slots.assign(kind >= 2 ? 3 : 27, ItemStack{});
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
            static const std::string root = [&] {
                const auto store = structure::TemplateStore::Find(config.structurePack, config.dataDir);
                return store ? std::filesystem::path(store->Directory()).parent_path().string() : std::string();
            }();
            if (root.empty())
                return false;
            std::ifstream in(std::filesystem::path(root) / "loot_tables" / (key + ".json"));
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
        if (entry->column.generated.empty())
            return;
        std::vector<GeneratedExtra> extras = std::move(entry->column.generated);
        entry->column.generated.clear();
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
}
