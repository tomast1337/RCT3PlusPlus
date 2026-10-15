// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_rows.hpp"

#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kCustomTrack = "RideSelectionWindow_BuildCustomTrack";
        constexpr std::string_view kPlaceAttraction = "RideSelectionWindow_BuildAttraction";
        /// The badge of items that came with an expansion, by the expansion's number less one.
        constexpr std::string_view kBadgePrefix = "ObjectSelectionWindow_AOPBadge_";
        constexpr int kBadges = 2;

        /// What each way of sitting on the grid is called, from 1; the rest give their footprint.
        constexpr std::array<std::string_view, 8> kPlacements = {
            "SceneryPlacement_PathEdgeInner", "SceneryPlacement_PathEdgeOuter", "SceneryPlacement_Wall", "SceneryPlacement_QuarterTile", "SceneryPlacement_HalfTile", "SceneryPlacement_PathCenter", "SceneryPlacement_Corner", "SceneryPlacement_PathInternalEdgeOuter",
        };

        /// Rows in name order whatever the case, headings among them by their own names.
        void sort_rows(std::vector<PickerRow>& rows)
        {
            const auto lower = [](std::u16string_view s) {
                std::u16string out(s);
                std::ranges::transform(out, out.begin(), [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c; });
                return out;
            };
            std::ranges::stable_sort(rows, [&](const PickerRow& a, const PickerRow& b) { return lower(a.name) < lower(b.name); });
            for (PickerRow& r : rows)
                sort_rows(r.children);
        }

        std::u16string name_of(const StringTable& strings, std::string_view key, std::string_view fallback)
        {
            const std::u16string_view text = strings.get_ignoring_case(key);
            return text.empty() ? strings::to_utf16(fallback) : std::u16string(text);
        }

    } // namespace

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
        return strings::to_utf16(std::to_string(item.width) + " x " + std::to_string(item.length));
    }

    std::vector<PickerRow> picker_rows(park::BuildList list, std::span<const park::BuildItem> items, const park::BuildOffer& offer, const StringTable& strings, const Currency& currency)
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
            row.icon = item.icon;
            row.item = i;
            switch (item.kind)
            {
                case park::BuildKind::TrackedRide:
                    row.size = std::u16string(strings.get(kCustomTrack));
                    break;
                case park::BuildKind::FlatRide:
                    row.size = std::u16string(strings.get(kPlaceAttraction));
                    break;
                case park::BuildKind::Shop:
                case park::BuildKind::Scenery:
                    row.size = placement_text(item, strings);
                    break;
            }
            if (item.price > 0)
                row.price = money_text(currency, item.price);
            if (item.addon >= 1 && item.addon <= kBadges)
                row.badge = std::string(kBadgePrefix) + std::to_string(item.addon - 1);

            if (item.group_icon.empty() && item.group_text.empty())
            {
                rows.push_back(std::move(row));
                continue;
            }
            const auto key = std::pair { item.group_icon, item.group_text };
            auto at = headings.find(key);
            if (at == headings.end())
            {
                PickerRow heading;
                heading.name = name_of(strings, item.group_text, item.group_icon);
                heading.icon = item.group_icon;
                heading.item = i;
                at = headings.emplace(key, rows.size()).first;
                rows.push_back(std::move(heading));
            }
            rows[at->second].children.push_back(std::move(row));
        }
        sort_rows(rows);
        return rows;
    }

} // namespace rct3::ui
