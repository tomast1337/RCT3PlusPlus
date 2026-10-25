// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_rows.hpp"

#include "strings/ascii.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <string_view>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kCustomTrack = "RideSelectionWindow_BuildCustomTrack";
        constexpr std::string_view kPlaceAttraction = "RideSelectionWindow_BuildAttraction";
        constexpr std::string_view kDesignsAvailable = "RideSelectionWindow_DesignsAvailable";
        constexpr std::u16string_view kNumber = u"%NUMBER%";
        /// The badge of items that came with an expansion, by the expansion's number less one.
        constexpr std::string_view kBadgePrefix = "ObjectSelectionWindow_AOPBadge_";
        constexpr int kBadges = 2;

        /// What each way of sitting on the grid is called, from 1; the rest give their footprint.
        constexpr std::array<std::string_view, 8> kPlacements = {
            "SceneryPlacement_PathEdgeInner", "SceneryPlacement_PathEdgeOuter", "SceneryPlacement_Wall", "SceneryPlacement_QuarterTile", "SceneryPlacement_HalfTile", "SceneryPlacement_PathCenter", "SceneryPlacement_Corner", "SceneryPlacement_PathInternalEdgeOuter",
        };

        /// The parts of the enclosure list: the icon and name of each heading, in the order they show.
        struct PartHeading
        {
            park::EnclosurePart part;
            std::string_view icon;
            std::string_view text;
        };
        constexpr std::array<PartHeading, 3> kEnclosureParts = { {
            { park::EnclosurePart::Fences, "EnclosureMaster_FenceIcon", "EnclosureMaster_FenceTT" },
            { park::EnclosurePart::Shelters, "EnclosureMaster_ShelterIcon", "EnclosureMaster_ShelterTT" },
            { park::EnclosurePart::Toys, "AnimalItems_General_Icon", "EnclosureMaster_ItemsTT" },
        } };

        constexpr std::string_view kRarity = "WildAnimalRarity";
        constexpr std::string_view kShelterNeeded = "WildAnimal_ShelterRequired";
        constexpr std::string_view kShelterName = "%SHELTERNAME%";
        /// Fences sort by strength; this keeps two places of it.
        constexpr float kStrengthRank = 100;

        /// Rows by rank, then in name order whatever the case; headings among them by their own.
        void sort_rows(std::vector<PickerRow>& rows)
        {
            const auto lower = [](std::u16string_view s) {
                std::u16string out(s);
                std::ranges::transform(out, out.begin(), [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c; });
                return out;
            };
            std::ranges::stable_sort(rows, [&](const PickerRow& a, const PickerRow& b) { return a.rank != b.rank ? a.rank < b.rank : lower(a.name) < lower(b.name); });
            for (PickerRow& r : rows)
                sort_rows(r.children);
        }

        std::u16string name_of(const StringTable& strings, std::string_view key, std::string_view fallback)
        {
            const std::u16string_view text = strings.get_ignoring_case(key);
            return text.empty() ? strings::to_utf16(fallback) : std::u16string(text);
        }

        /// The weakest offered fence at least as strong as `strength`; nothing when none is.
        const park::BuildItem* fence_for(float strength, std::span<const park::BuildItem> items, const park::BuildOffer& offer)
        {
            const park::BuildItem* best = nullptr;
            for (const park::BuildItem& f : items)
            {
                if (f.kind == park::BuildKind::EnclosureFence && f.strength >= strength && offer.offers(f) && (best == nullptr || f.strength < best->strength))
                    best = &f;
            }
            return best;
        }

        std::u16string name_text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get_ignoring_case(key));
        }

        /// The name of the shelter called `symbol`, whatever its case; empty when it is not installed.
        std::string_view shelter_text(std::string_view symbol, std::span<const park::BuildItem> items)
        {
            for (const park::BuildItem& s : items)
            {
                if (s.kind == park::BuildKind::Shelter && strings::equals_ignoring_case(s.symbol, symbol))
                    return s.name_text;
            }
            return { };
        }

        /// What a species' heading adds to its name: the fence it needs.
        std::u16string fence_note(const park::BuildItem& animal, std::span<const park::BuildItem> items, const park::BuildOffer& offer, const StringTable& strings)
        {
            const park::BuildItem* fence = fence_for(animal.strength, items, offer);
            return fence == nullptr ? std::u16string { } : u" (" + name_text(strings, fence->name_text) + u")";
        }

        /// The line under a species' heading: the shelter it needs.
        std::u16string shelter_note(const park::BuildItem& animal, std::span<const park::BuildItem> items, const StringTable& strings)
        {
            const std::string_view shelter = shelter_text(animal.needs, items);
            if (shelter.empty())
                return { };
            std::u16string needs(strings.get(kShelterNeeded));
            const std::u16string field = strings::to_utf16(kShelterName);
            if (const auto at = needs.find(field); at != std::u16string::npos)
                needs.replace(at, field.size(), name_text(strings, shelter));
            return needs;
        }

    } // namespace

    bool has_theme_filters(park::BuildList list) noexcept
    {
        using enum park::BuildList;
        switch (list)
        {
            case JuniorRides:
            case ThrillRides:
            case GentleRides:
            case FoodShops:
            case DrinkShops:
            case Stalls:
            case Trees:
            case Fences:
            case Walls:
            case PathExtras:
            case Props:
            case ParkEntrances:
                return true;
            default:
                return false;
        }
    }

    std::vector<PickerTheme> picker_themes(std::span<const park::BuildItem> items)
    {
        std::map<std::int32_t, PickerTheme> by_number;
        for (const park::BuildItem& item : items)
        {
            if (item.style_index >= 0 && !item.style.empty())
                by_number.try_emplace(item.style_index, PickerTheme { item.style, strings::to_utf16(item.style_title) });
        }
        std::vector<PickerTheme> themes;
        for (auto& [_, theme] : by_number)
            themes.push_back(std::move(theme));
        return themes;
    }

    std::string_view object_filter(const park::BuildItem& item) noexcept
    {
        // Matched to the filters' names by the items of each kind or category in the install: kind 16 holds
        // the inverted and suspended coasters, category 6 the lamps, 31 the benches, 36 the towers and so on.
        const bool ride = item.kind == park::BuildKind::TrackedRide || item.kind == park::BuildKind::FlatRide;
        if (ride)
        {
            switch (item.type)
            {
                case 0:
                    return "Transport_Standard";
                case 3:
                    return "Coaster_Steel";
                case 15:
                    return "Coaster_Wooden";
                case 16:
                    return "Coaster_Suspended";
                case 17:
                    return "Coaster_Water";
                case 21:
                    return "Transport_Safari";
                default:
                    return { };
            }
        }
        if (item.kind != park::BuildKind::Scenery || item.list == park::BuildList::Billboards)
            return { };
        switch (item.type)
        {
            case 0:
                return "Scenery_Tree";
            case 1:
                return "Scenery_Plant";
            case 2:
                return "Scenery_Shrub";
            case 3:
                return "Scenery_Flower";
            case 5:
                return "Scenery_Misc";
            case 6:
                return "Scenery_Lamp";
            case 7:
                return "Scenery_SmallProp";
            case 8:
                return "Scenery_MediumProp";
            case 9:
                return "Scenery_LargeProp";
            case 10:
                return "Scenery_AnimatronicProp";
            case 30:
                return "Scenery_Bin";
            case 31:
                return "Scenery_Seat";
            case 32:
                return "Scenery_Sign";
            case 33:
                return "Scenery_PhotoSpot";
            case 34:
                return "Scenery_Walls";
            case 35:
                return "Scenery_Roof";
            case 36:
                return "Scenery_Corners";
            default:
                return { };
        }
    }

    std::string_view rarity_text(float rarity) noexcept
    {
        if (rarity > 0.79f)
            return "WildAnimalRarity_Endangered";
        if (rarity > 0.64f)
            return "WildAnimalRarity_Rare";
        if (rarity >= 0.40f)
            return "WildAnimalRarity_Moderate";
        return "WildAnimalRarity_Common";
    }

    std::optional<park::BuildList> build_list(ToolItem item) noexcept
    {
        using park::BuildList;
        switch (item)
        {
            case ToolItem::Coasters:
                return BuildList::Coasters;
            case ToolItem::JuniorRides:
                return BuildList::JuniorRides;
            case ToolItem::ThrillRides:
                return BuildList::ThrillRides;
            case ToolItem::WaterRides:
                return BuildList::WaterRides;
            case ToolItem::GentleRides:
                return BuildList::GentleRides;
            case ToolItem::TransportRides:
                return BuildList::TransportRides;
            case ToolItem::OtherRides:
                return BuildList::OtherRides;
            case ToolItem::PoolRides:
                return BuildList::PoolRides;
            case ToolItem::Enclosures:
                return BuildList::Enclosures;
            case ToolItem::Animals:
                return BuildList::Animals;
            case ToolItem::FoodShops:
                return BuildList::FoodShops;
            case ToolItem::DrinkShops:
                return BuildList::DrinkShops;
            case ToolItem::Stalls:
                return BuildList::Stalls;
            case ToolItem::Facilities:
                return BuildList::Facilities;
            case ToolItem::Plants:
                return BuildList::Trees;
            case ToolItem::Fences:
                return BuildList::Fences;
            case ToolItem::Buildings:
                return BuildList::Walls;
            case ToolItem::PathExtras:
                return BuildList::PathExtras;
            case ToolItem::SceneryItems:
                return BuildList::Props;
            case ToolItem::RideEvents:
                return BuildList::RideEvents;
            case ToolItem::Fireworks:
                return BuildList::Fireworks;
            case ToolItem::GroundSurfaces:
                return BuildList::GroundSurfaces;
            case ToolItem::Billboards:
                return BuildList::Billboards;
            default:
                return std::nullopt;
        }
    }

    std::u16string placement_text(const park::BuildItem& item, const StringTable& strings)
    {
        if (item.placement >= 1 && item.placement <= kPlacements.size())
            return std::u16string(strings.get(kPlacements[item.placement - 1]));
        return strings::to_utf16(std::format("{} x {}", item.width, item.length));
    }

    std::vector<PickerRow> picker_rows(park::BuildList list, std::span<const park::BuildItem> items, const park::BuildOffer& offer, const StringTable& strings, const Currency& currency, const std::map<std::string, std::size_t, strings::LessIgnoringCase>& design_counts)
    {
        std::vector<PickerRow> rows;
        std::map<std::pair<std::string, std::string>, std::size_t> headings; // by set icon and name
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const park::BuildItem& item = items[i];
            if (item.list != list || !offer.offers(item))
                continue;
            PickerRow row;
            row.name = name_of(strings, item.name_text, item.symbol);
            row.key = item.symbol;
            row.icon = item.icon;
            row.item = i;
            switch (item.kind)
            {
                case park::BuildKind::TrackedRide:
                    row.size = std::u16string(strings.get(kCustomTrack));
                    if (const auto designs = design_counts.find(item.symbol); designs != design_counts.end())
                    {
                        std::u16string available(strings.get(kDesignsAvailable));
                        if (const auto at = available.find(kNumber); at != std::u16string::npos)
                            available.replace(at, kNumber.size(), strings::to_utf16(std::to_string(designs->second)));
                        row.size = std::move(available);
                    }
                    break;
                case park::BuildKind::FlatRide:
                    row.size = std::u16string(strings.get(kPlaceAttraction));
                    break;
                case park::BuildKind::Shop:
                case park::BuildKind::Scenery:
                case park::BuildKind::Shelter:
                    row.size = placement_text(item, strings);
                    break;
                case park::BuildKind::EnclosureFence:
                    row.rank = static_cast<int>(item.strength * kStrengthRank);
                    break;
                case park::BuildKind::AnimalToy:
                    break;
                case park::BuildKind::Animal:
                    row.size = std::u16string(strings.get(kRarity)) + u' ' + std::u16string(strings.get(rarity_text(item.rarity)));
                    break;
            }
            if (item.price > 0)
                row.price = money_text(currency, item.price);
            row.cost = item.price;
            row.style = item.style;
            row.addon = item.addon;
            row.kind = object_filter(item);
            if (item.addon >= 1 && item.addon <= kBadges)
                row.badge = std::string(kBadgePrefix) + std::to_string(item.addon - 1);

            // Enclosure items sit under the heading of their part, in the parts' order.
            std::string group_icon = item.group_icon;
            std::string group_text = item.group_text;
            int heading_rank = 0;
            for (std::size_t p = 0; p < kEnclosureParts.size(); ++p)
            {
                if (kEnclosureParts[p].part == item.part)
                {
                    group_icon = kEnclosureParts[p].icon;
                    group_text = kEnclosureParts[p].text;
                    heading_rank = static_cast<int>(p);
                }
            }
            if (group_icon.empty() && group_text.empty())
            {
                rows.push_back(std::move(row));
                continue;
            }
            const auto key = std::pair { group_icon, group_text };
            auto at = headings.find(key);
            if (at == headings.end())
            {
                PickerRow heading;
                heading.name = name_of(strings, group_text, group_icon);
                heading.key = group_text.empty() ? group_icon : group_text;
                heading.icon = group_icon;
                heading.item = i;
                heading.rank = heading_rank;
                if (item.kind == park::BuildKind::Animal)
                {
                    heading.name += fence_note(item, items, offer, strings);
                    heading.size = shelter_note(item, items, strings);
                }
                at = headings.emplace(key, rows.size()).first;
                rows.push_back(std::move(heading));
            }
            rows[at->second].children.push_back(std::move(row));
        }
        sort_rows(rows);
        return rows;
    }

} // namespace rct3::ui
