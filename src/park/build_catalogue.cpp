// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/build_catalogue.hpp"

#include "ovl/animal_items.hpp"
#include "ovl/animal_species.hpp"
#include "ovl/animated_ride.hpp"
#include "ovl/enclosure_fence.hpp"
#include "ovl/integer_value.hpp"
#include "ovl/piece_set.hpp"
#include "ovl/room_type.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/special_attraction.hpp"
#include "ovl/stall.hpp"
#include "ovl/text.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/item_category.hpp"
#include "park/pools.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <map>
#include <system_error>
#include <utility>

namespace rct3::park
{

    namespace
    {

        constexpr std::string_view kCommonEnding = ".common.ovl";
        constexpr std::string_view kStyleFolder = "Style";
        constexpr std::string_view kStyleOverlay = "Style";
        constexpr std::string_view kPlainStyle = "Style/Vanilla/Style"; ///< The theme without a theme
        /// A theme's overlay: its number, its name and its title.
        constexpr std::string_view kStyleIndex = "StyleIndex";
        constexpr std::string_view kStyleName = "StyleInternalName";
        constexpr std::string_view kStyleTitle = "StyleName";
        constexpr std::string_view kStyleFence = "FenceName"; ///< The fence new rides of the theme take
        constexpr std::array<std::string_view, 2> kRideFolders = { "Tracks/Coasters", "Tracks/TrackedRides" };
        /// Each folder below it holds one kind of enclosure fence, in the overlay named after the folder with
        /// this ending; the rest of its overlays are the pieces' shapes.
        constexpr std::string_view kFenceFolder = "Enclosures/Fences";
        constexpr std::string_view kFenceEnding = "_Stub";
        constexpr std::string_view kEnclosureDeleteText = "EnclosureDeleteTool_TT";
        constexpr std::string_view kEnclosureDeleteIcon = "GUI2_DeleteSelection";
        constexpr std::string_view kPoolPanelText = "Pool_PoolPanel";
        constexpr std::string_view kPoolRoomText = "Pool_ChangingRoomPanel";
        constexpr std::string_view kPoolExtraText = "Pool_ExtraPanel";
        constexpr std::string_view kJacuzziIcon = "Icon_Default_Jacuzzi";
        /// The overlays of shelters, animal species and toys.
        constexpr std::array<std::string_view, 3> kAnimalOverlays = { "Enclosures/Shelters/Shelters", "WildAnimals/WildAnimals", "WildAnimals/Items/Items" };

        std::string lower(std::string_view s)
        {
            std::string out(s);
            std::ranges::transform(out, out.begin(), [](char c) { return strings::ascii_lower(c); });
            return out;
        }

        std::string key(std::string_view overlay, std::string_view symbol)
        {
            return lower(overlay) + '|' + lower(symbol);
        }

        /// The scenery items of both parts, by name, for the attractions that are placed as one. Attractions
        /// spell the name in their own case now and then.
        using ByName = std::map<std::string_view, const ovl::SceneryItem*, strings::LessIgnoringCase>;
        ByName by_name(const std::vector<ovl::SceneryItemEntry>& entries)
        {
            ByName out;
            for (const auto& e : entries)
            {
                if (e.item)
                    out.emplace(e.name, &*e.item);
            }
            return out;
        }

        BuildItem attraction_item(BuildList list, BuildKind kind, std::string_view symbol, std::string_view path, const ovl::AttractionInfo& info, const ovl::SceneryItem* placed)
        {
            BuildItem item;
            item.list = list;
            item.kind = kind;
            item.symbol = symbol;
            item.overlay = path;
            item.name_text = info.name_text;
            item.description_text = info.description_text;
            item.icon = info.icon;
            item.type = info.type_id;
            item.addon = info.addon;
            if (placed != nullptr)
            {
                if (item.icon.empty())
                    item.icon = placed->icon;
                if (item.name_text.empty())
                    item.name_text = placed->name_text;
                item.price = placed->cost;
                if (placed->addon != 0)
                    item.addon = placed->addon;
                item.placement = placed->positioning;
                item.width = placed->x_squares;
                item.length = placed->z_squares;
                item.water_only = (placed->flags & ovl::kItemWaterOnly) != 0;
            }
            item.scenery = info.style.empty() ? symbol : info.style;
            return item;
        }

        void add_scenery(const ovl::SceneryItem& s, std::string_view symbol, std::string_view path, std::vector<BuildItem>& items)
        {
            if (s.icon.empty())
                return;
            BuildItem item;
            item.kind = BuildKind::Scenery;
            item.symbol = symbol;
            item.overlay = path;
            item.name_text = s.name_text;
            item.icon = s.icon;
            item.price = s.cost;
            item.group_icon = s.group_icon;
            item.group_text = s.group_text;
            item.placement = s.positioning;
            item.width = s.x_squares;
            item.length = s.z_squares;
            item.addon = s.addon;
            item.type = s.type;
            if ((s.flags & kBillboardFlag) != 0)
            {
                item.list = BuildList::Billboards;
                items.push_back(item);
            }
            if (const auto list = scenery_list(s.type))
            {
                item.list = *list;
                items.push_back(std::move(item));
            }
        }

        void add_enclosure_items(const ovl::Overlay& overlay, const ovl::Part& part, std::string_view path, const ByName& placed, std::vector<BuildItem>& items)
        {
            const auto item_of = [&](BuildKind kind, std::string_view symbol) {
                BuildItem item;
                item.list = kind == BuildKind::Animal ? BuildList::Animals : BuildList::Enclosures;
                item.kind = kind;
                item.symbol = symbol;
                item.overlay = path;
                return item;
            };
            for (const auto& e : ovl::read_enclosure_fences(overlay, part))
            {
                if (!e.fence)
                    continue;
                BuildItem item = item_of(BuildKind::EnclosureFence, e.name);
                item.part = EnclosurePart::Fences;
                item.name_text = e.fence->name_text;
                item.icon = e.fence->icon;
                item.price = e.fence->fence_cost;
                item.refund = e.fence->fence_refund;
                item.strength = e.fence->strength;
                items.push_back(std::move(item));
            }
            // A shelter is listed as the scenery item it is placed as, which parks record.
            for (const auto& e : ovl::read_shelters(overlay, part))
            {
                const auto it = placed.find(e.scenery);
                if (it == placed.end() || it->second->icon.empty())
                    continue;
                const ovl::SceneryItem& s = *it->second;
                BuildItem item = item_of(BuildKind::Shelter, it->first);
                item.part = EnclosurePart::Shelters;
                item.scenery = e.hut;
                item.name_text = s.name_text;
                item.icon = s.icon;
                item.price = s.cost;
                item.placement = s.positioning;
                item.width = s.x_squares;
                item.length = s.z_squares;
                item.addon = s.addon;
                items.push_back(std::move(item));
            }
            for (const auto& e : ovl::read_animal_toys(overlay, part))
            {
                if (!e.toy)
                    continue;
                BuildItem item = item_of(BuildKind::AnimalToy, e.name);
                item.part = EnclosurePart::Toys;
                item.name_text = e.toy->name_text;
                item.description_text = e.toy->description_text;
                item.icon = e.toy->icon;
                item.price = e.toy->price;
                items.push_back(std::move(item));
            }
            for (const auto& e : ovl::read_animal_species(overlay, part))
            {
                if (!e.species)
                    continue;
                const ovl::AnimalSpecies& s = *e.species;
                for (const auto& v : s.variants)
                {
                    if (!v)
                        continue;
                    BuildItem item = item_of(BuildKind::Animal, e.name);
                    item.name_text = v->description_text;
                    item.description_text = s.description_text;
                    item.icon = v->icon.empty() ? s.icon : v->icon;
                    item.price = v->price;
                    item.group_icon = s.icon;
                    item.group_text = s.name_text;
                    item.needs = s.shelter;
                    item.strength = s.fence_strength;
                    item.rarity = v->rarity;
                    items.push_back(std::move(item));
                }
            }
        }

        /// The child of `dir` named `name` whatever its case; nothing when there is none.
        std::optional<std::filesystem::path> child(const std::filesystem::path& dir, std::string_view name)
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (strings::equals_ignoring_case(entry.path().filename().string(), name))
                    return entry.path();
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> find(const std::filesystem::path& root, std::string_view relative)
        {
            std::filesystem::path at = root;
            for (const auto& part : std::filesystem::path(relative))
            {
                const auto next = child(at, part.string());
                if (!next)
                    return std::nullopt;
                at = *next;
            }
            return at;
        }

        bool is_common(const std::filesystem::path& file)
        {
            const std::string name = file.filename().string();
            return name.size() > kCommonEnding.size() && strings::equals_ignoring_case(std::string_view(name).substr(name.size() - kCommonEnding.size()), kCommonEnding);
        }

        std::string stem_of(const std::filesystem::path& file)
        {
            const std::string name = file.filename().string();
            return name.substr(0, name.size() - kCommonEnding.size());
        }

        void read_file(const std::filesystem::path& root, const std::filesystem::path& file, std::vector<BuildItem>& items)
        {
            auto overlay = ovl::Overlay::load(file);
            if (!overlay)
                return;
            std::string path = std::filesystem::relative(file.parent_path(), root).generic_string();
            path += '/';
            path += stem_of(file);
            add_build_items(*overlay, path, items);
        }

    } // namespace

    std::optional<BuildList> ride_list(std::uint32_t kind) noexcept
    {
        switch (kind)
        {
            case 0:
            case 21:
                return BuildList::TransportRides;
            case 1:
                return BuildList::GentleRides;
            case 2:
                return BuildList::WaterRides;
            case 3:
            case 15:
            case 16:
            case 17:
                return BuildList::Coasters;
            case 4:
                return BuildList::ThrillRides;
            case 5:
                return BuildList::OtherRides;
            case 6:
                return BuildList::JuniorRides;
            case 19:
                return BuildList::PoolRides;
            default:
                return std::nullopt;
        }
    }

    std::optional<BuildList> shop_list(std::uint32_t kind) noexcept
    {
        switch (kind)
        {
            case 7:
                return BuildList::FoodShops;
            case 8:
                return BuildList::DrinkShops;
            case 9:
                return BuildList::Stalls;
            case 11:
            case 12:
            case 13:
            case 14:
                return BuildList::Facilities;
            default:
                return std::nullopt;
        }
    }

    std::optional<BuildList> scenery_list(std::uint32_t category) noexcept
    {
        switch (category)
        {
            case 0:
            case 1:
            case 2:
            case 3:
                return BuildList::Trees;
            case 4:
                return BuildList::Fences;
            case 5:
            case 34:
            case 35:
            case 36:
                return BuildList::Walls;
            case 6:
            case 30:
            case 31:
            case 32:
            case 33:
            case 37:
                return BuildList::PathExtras;
            case 7:
            case 8:
            case 9:
            case 10:
            case 44:
                return BuildList::Props;
            case 20:
            case 22:
                return BuildList::ParkEntrances;
            case 28:
                return BuildList::RideEvents;
            case 29:
            case 41:
            case 42:
                return BuildList::Fireworks;
            case 43:
                return BuildList::GroundSurfaces;
            default:
                return std::nullopt;
        }
    }

    std::optional<std::uint32_t> special_attraction_kind(const std::filesystem::path& file, std::string_view name)
    {
        const auto overlay = ovl::Overlay::load(file);
        if (!overlay)
            return std::nullopt;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
            if (part)
                for (const auto& e : ovl::read_special_attractions(*overlay, *part))
                    if (e.attraction && strings::equals_ignoring_case(e.name, name))
                        return e.attraction->info.type_id;
        return std::nullopt;
    }

    void add_build_items(const ovl::Overlay& overlay, std::string_view path, std::vector<BuildItem>& items)
    {
        std::vector<const ovl::Part*> parts { &overlay.common() };
        if (const auto unique = overlay.unique())
            parts.push_back(&*unique);

        std::vector<ovl::SceneryItemEntry> scenery;
        for (const ovl::Part* part : parts)
        {
            auto more = ovl::read_scenery_items(overlay, *part);
            std::ranges::move(more, std::back_inserter(scenery));
        }
        const auto placed = by_name(scenery);
        const std::size_t first = items.size();
        // The scenery item an attraction is placed as: the one it names, or, in files where that is not read,
        // the one of the same name.
        const auto placed_as = [&](std::string_view named, std::string_view symbol) -> const ovl::SceneryItem* {
            const auto it = placed.find(named.empty() ? symbol : named);
            return it == placed.end() ? nullptr : it->second;
        };

        for (const auto& e : scenery)
        {
            if (e.item)
                add_scenery(*e.item, e.name, path, items);
        }
        for (const ovl::Part* part : parts)
        {
            for (const auto& e : ovl::read_tracked_rides(overlay, *part))
            {
                if (!e.ride)
                    continue;
                if (const auto list = ride_list(e.ride->info.type_id))
                    items.push_back(attraction_item(*list, BuildKind::TrackedRide, e.name, path, e.ride->info, placed_as(e.ride->info.style, e.name)));
            }
            for (const auto& e : ovl::read_animated_rides(overlay, *part))
            {
                if (!e.ride)
                    continue;
                if (const auto list = ride_list(e.ride->info.type_id))
                    items.push_back(attraction_item(*list, BuildKind::FlatRide, e.name, path, e.ride->info, placed_as(e.ride->info.style, e.name)));
            }
            for (const auto& e : ovl::read_special_attractions(overlay, *part))
            {
                if (!e.attraction)
                    continue;
                const auto& info = e.attraction->info;
                if (const auto list = ride_list(info.type_id))
                    items.push_back(attraction_item(*list, BuildKind::FlatRide, e.name, path, info, placed_as(info.style, e.name)));
                else if (const auto shop = shop_list(info.type_id))
                    items.push_back(attraction_item(*shop, BuildKind::Shop, e.name, path, info, placed_as(info.style, e.name)));
            }
            add_enclosure_items(overlay, *part, path, placed, items);
            for (const auto& e : ovl::read_pool_types(overlay, *part))
            {
                if (!e.pool)
                    continue;
                // The pool panel picks these by symbol and shows the editor's own pictures; the icons here are
                // for scripts and lists that name them.
                const auto tool = [&](std::string_view symbol, std::optional<PoolSquare> square) -> BuildItem& {
                    BuildItem& item = items.emplace_back();
                    item.list = BuildList::Pools;
                    item.kind = BuildKind::PoolSquare;
                    item.symbol = symbol;
                    item.overlay = path;
                    item.name_text = symbol;
                    item.icon = square ? std::string(e.pool->icon) : std::string(kEnclosureDeleteIcon);
                    item.group_icon = e.pool->icon;
                    item.group_text = kPoolPanelText;
                    item.price = square ? static_cast<std::int32_t>(pool_build_cost(e.pool->prices, *square)) : 0;
                    item.pool_prices.assign(e.pool->prices.begin(), e.pool->prices.end());
                    return item;
                };
                for (std::int32_t depth = 1; depth <= 3; ++depth)
                    tool(kPoolWater[static_cast<std::size_t>(depth - 1)], PoolSquare { .kind = PoolKind::Water, .depth = depth });
                tool(kPoolTiles, PoolSquare { .kind = PoolKind::Tile, .depth = 2 });
                tool(kPoolDelete, std::nullopt);
                // shortcut: of the extras laid as squares, only the spa pool; the high diving board stands on
                // raised squares, add it with the surface height buttons.
                BuildItem& jacuzzi = tool(kPoolJacuzzi, PoolSquare { .kind = PoolKind::Jacuzzi, .depth = 2 });
                jacuzzi.icon = kJacuzziIcon;
                jacuzzi.group_icon = kJacuzziIcon;
                jacuzzi.group_text = kPoolExtraText;
                // The extras are the scenery items of the pool type's overlay that say where in a pool they go.
                for (const auto& [name, item] : placed)
                {
                    const auto words = std::ranges::find(item->parameters, std::string_view("PoolExtra"), &ovl::SceneryItemParameter::name);
                    if (words == item->parameters.end() || item->icon.empty())
                        continue;
                    BuildItem& extra = items.emplace_back();
                    extra.list = BuildList::Pools;
                    extra.kind = BuildKind::PoolExtra;
                    extra.symbol = name;
                    extra.overlay = path;
                    extra.name_text = item->name_text;
                    extra.icon = item->icon;
                    extra.group_icon = kJacuzziIcon;
                    extra.group_text = kPoolExtraText;
                    extra.price = item->cost;
                    extra.pool_extra = words->value;
                }
            }
            // Changing rooms are placed as the scenery item they name, and show its icon.
            // shortcut: built free; the room's own price is not read yet, take it once its field is known.
            for (const auto& e : ovl::read_changing_rooms(overlay, *part))
            {
                const ovl::SceneryItem* room = e.room ? placed_as(e.room->scenery_item, e.name) : nullptr;
                if (room == nullptr || room->icon.empty())
                    continue;
                BuildItem& item = items.emplace_back();
                item.list = BuildList::Pools;
                item.kind = BuildKind::PoolRoom;
                item.symbol = e.room->scenery_item.empty() ? e.name : e.room->scenery_item;
                item.overlay = path;
                item.name_text = e.room->name_text.empty() ? room->name_text : e.room->name_text;
                item.icon = room->icon;
                item.group_icon = room->icon;
                item.group_text = kPoolRoomText;
            }
            for (const auto& e : ovl::read_stalls(overlay, *part))
            {
                if (!e.stall)
                    continue;
                if (const auto list = shop_list(e.stall->info.type_id))
                {
                    BuildItem& item = items.emplace_back(attraction_item(*list, BuildKind::Shop, e.name, path, e.stall->info, placed_as(e.stall->info.style, e.name)));
                    item.stall = true;
                    for (const ovl::StallGoods& g : e.stall->goods)
                        item.goods.push_back({ .item = std::string(g.item), .price = g.price });
                }
            }
        }
        // Rides take the first entrance and exit of their own theme.
        const auto first_of = [&](ItemCategory category) {
            const auto found = std::ranges::find_if(scenery, [&](const ovl::SceneryItemEntry& e) { return e.item && e.item->type == std::to_underlying(category); });
            return found == scenery.end() ? std::string { } : std::string(found->name);
        };
        const std::string entrance = first_of(ItemCategory::RideEntrance);
        const std::string exit = first_of(ItemCategory::RideExit);
        // Everything a theme's overlay offers is of that theme.
        std::int32_t style_index = -1;
        std::string style, style_title, fence;
        for (const ovl::Part* part : parts)
        {
            for (const ovl::IntegerItem& i : ovl::read_integers(overlay, *part))
            {
                if (i.name == kStyleIndex)
                    style_index = i.value;
            }
            for (const ovl::TextItem& t : ovl::read_texts(overlay, *part))
            {
                if (t.value && t.name == kStyleName)
                    style = strings::to_utf8(*t.value);
                else if (t.value && t.name == kStyleTitle)
                    style_title = strings::to_utf8(*t.value);
                else if (t.value && t.name == kStyleFence)
                    fence = strings::to_utf8(*t.value);
            }
        }
        for (std::size_t i = first; i < items.size(); ++i)
        {
            items[i].style_index = style_index;
            items[i].style = style;
            items[i].style_title = style_title;
            items[i].fence = fence;
            // Here when this overlay holds the fence; else found in the one that does, below.
            if (!fence.empty() && std::ranges::any_of(scenery, [&](const ovl::SceneryItemEntry& e) { return strings::equals_ignoring_case(e.name, fence); }))
                items[i].fence_overlay = path;
            if ((items[i].kind == BuildKind::FlatRide || items[i].kind == BuildKind::TrackedRide) && !entrance.empty() && !exit.empty())
            {
                items[i].entrance = entrance;
                items[i].exit = exit;
                items[i].door_overlay = path;
            }
        }
    }

    void name_styles(std::vector<BuildItem>& items)
    {
        std::map<std::int32_t, std::pair<std::string, std::string>> named;
        for (const BuildItem& item : items)
        {
            if (item.style_index >= 0 && !item.style.empty())
                named.try_emplace(item.style_index, item.style, item.style_title);
        }
        for (BuildItem& item : items)
        {
            if (const auto found = named.find(item.style_index); item.style.empty() && found != named.end())
                std::tie(item.style, item.style_title) = found->second;
        }
    }

    std::vector<BuildItem> read_build_catalogue(const std::filesystem::path& root)
    {
        std::vector<BuildItem> items;
        std::error_code ec;
        if (const auto styles = find(root, kStyleFolder))
        {
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(*styles, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (it->is_regular_file(ec) && is_common(it->path()) && strings::equals_ignoring_case(stem_of(it->path()), kStyleOverlay))
                    files.push_back(it->path());
            }
            std::ranges::sort(files);
            for (const auto& file : files)
                read_file(root, file, items);
        }
        for (const std::string_view folder : kRideFolders)
        {
            const auto rides = find(root, folder);
            if (!rides)
                continue;
            std::vector<std::filesystem::path> dirs;
            for (const auto& entry : std::filesystem::directory_iterator(*rides, ec))
            {
                if (entry.is_directory(ec))
                    dirs.push_back(entry.path());
            }
            std::ranges::sort(dirs);
            for (const auto& dir : dirs)
            {
                std::vector<std::filesystem::path> files;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                {
                    if (entry.is_regular_file(ec) && is_common(entry.path()))
                        files.push_back(entry.path());
                }
                if (files.size() == 1)
                    read_file(root, files.front(), items);
                else if (const auto named = std::ranges::find_if(files, [&](const auto& f) { return strings::equals_ignoring_case(stem_of(f), dir.filename().string()); }); named != files.end())
                    read_file(root, *named, items);
            }
        }
        if (const auto fences = find(root, kFenceFolder))
        {
            std::vector<std::filesystem::path> files;
            for (const auto& dir : std::filesystem::directory_iterator(*fences, ec))
            {
                if (!dir.is_directory(ec))
                    continue;
                const std::string wanted = dir.path().filename().string() + std::string(kFenceEnding);
                if (const auto file = child(dir.path(), wanted + std::string(kCommonEnding)))
                    files.push_back(*file);
            }
            std::ranges::sort(files);
            for (const auto& file : files)
                read_file(root, file, items);
            if (!files.empty())
            {
                BuildItem& del = items.emplace_back();
                del.list = BuildList::Enclosures;
                del.kind = BuildKind::EnclosureFence;
                del.symbol = kEnclosureDelete;
                del.name_text = kEnclosureDeleteText;
                del.icon = kEnclosureDeleteIcon;
                del.part = EnclosurePart::Fences;
                del.strength = -1; // listed before the weakest fence
            }
        }
        for (const std::string_view overlay : { kAnimalOverlays[0], kAnimalOverlays[1], kAnimalOverlays[2], kPoolOverlay })
        {
            if (const auto file = find(root, std::string(overlay) + std::string(kCommonEnding)))
                read_file(root, *file, items);
        }
        name_styles(items);
        // Rides of a theme without an entrance and exit of its own take the plain theme's, as parks show; so do
        // rides of no theme for their fence. A theme may name a fence another theme's overlay holds.
        const auto plain = std::ranges::find_if(items, [](const BuildItem& i) { return !i.entrance.empty() && strings::equals_ignoring_case(i.door_overlay, kPlainStyle); });
        const auto plain_fence = std::ranges::find_if(items, [](const BuildItem& i) { return !i.fence.empty() && strings::equals_ignoring_case(i.overlay, kPlainStyle); });
        std::map<std::int32_t, std::string> theme_fences;
        for (const BuildItem& item : items)
            if (item.style_index >= 0 && !item.fence.empty())
                theme_fences.try_emplace(item.style_index, item.fence);
        for (BuildItem& item : items)
        {
            if (const auto found = theme_fences.find(item.style_index); item.fence.empty() && found != theme_fences.end())
                item.fence = found->second;
            if ((item.kind == BuildKind::FlatRide || item.kind == BuildKind::TrackedRide) && item.entrance.empty() && plain != items.end())
            {
                item.entrance = plain->entrance;
                item.exit = plain->exit;
                item.door_overlay = plain->door_overlay;
            }
            if ((item.kind == BuildKind::FlatRide || item.kind == BuildKind::TrackedRide) && item.fence.empty() && plain_fence != items.end())
                item.fence = plain_fence->fence;
            if (const auto holder = std::ranges::find_if(items, [&](const BuildItem& i) { return !i.fence_overlay.empty() && strings::equals_ignoring_case(i.fence, item.fence); }); !item.fence.empty() && item.fence_overlay.empty() && holder != items.end())
                item.fence_overlay = holder->fence_overlay;
        }
        return items;
    }

    BuildOffer::BuildOffer(const save::RequiredContent& content)
        : known_(!content.entries.empty())
    {
        for (const save::AssetRequirement& e : content.entries)
        {
            const bool species = e.kind == save::AssetKind::AnimalSpecies;
            species_known_ = species_known_ || species;
            listed_.insert(key(e.overlay_stem, e.symbol));
            if (species ? e.unlocked : e.available && e.invented && !e.hidden)
                offered_.insert(key(e.overlay_stem, e.symbol));
        }
    }

    bool BuildOffer::offers(const BuildItem& item) const
    {
        switch (item.kind)
        {
            case BuildKind::Animal:
                return !species_known_ || offered_.contains(key(item.overlay, item.symbol));
            case BuildKind::EnclosureFence:
            case BuildKind::Shelter:
            case BuildKind::AnimalToy:
            case BuildKind::PoolSquare:
            case BuildKind::PoolExtra:
            case BuildKind::PoolRoom:
                return true; // parks list these without saying whether they may be built
            default:
            {
                // Parks saved before an expansion do not list its items, and offer them all.
                const std::string k = key(item.overlay, item.symbol);
                return !listed_.contains(k) || offered_.contains(k);
            }
        }
    }

} // namespace rct3::park
