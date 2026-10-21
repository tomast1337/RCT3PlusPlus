// SPDX-License-Identifier: GPL-3.0-or-later
//
// What each button of the tool row offers when pressed: a row of smaller buttons that opens above it. The
// paths, map and leave buttons have no menu; they act at once. Items are listed left to right. Each shows
// its icon and, as help, the string named after the icon with "Help" added, unless it names its own.
//
// The view controls are switches: each has a second icon for while what it controls is shown.
#pragma once

#include "ui/screens/tool_row.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace rct3::ui
{

    enum class ToolItem : std::uint8_t
    {
        // Delete
        DeleteOne,
        DeleteArea,
        // Land
        Waterfalls,
        Water,
        PaintGround,
        ShapeGround,
        // Scenery
        Fireworks,
        SavedStructures,
        Billboards,
        GroundSurfaces,
        RideEvents,
        SceneryItems,
        PathExtras,
        Buildings,
        Fences,
        Plants,
        // Shops
        Facilities,
        Stalls,
        DrinkShops,
        FoodShops,
        // Rides
        Enclosures,
        Animals,
        Pools,
        ImportedTracks,
        OtherRides,
        PoolRides,
        TransportRides,
        GentleRides,
        WaterRides,
        ThrillRides,
        JuniorRides,
        Coasters,
        // Park management
        Challenges,
        Guests,
        Attractions,
        Research,
        Objectives,
        Staff,
        Finances,
        ParkOperations,
        // Files
        Save,
        Load,
        Options,
        About,
        // View
        AnimalTiles,
        SeeThroughGround,
        Scenery,
        Supports,
        People,
        PathHeights,
        RideHeights,
        Contours,
        Grid,
        Money,         ///< Amounts floating up where money is spent or paid back
        GuestPayments, ///< Amounts floating up where guests pay
    };

    struct ToolMenuItem
    {
        ToolItem item = ToolItem::DeleteOne;
        std::string_view icon;
        std::string_view shown_icon; ///< Switches only: the icon while what it controls is shown; the same icon shows it pressed in
        std::string_view help;       ///< String key, when not the icon's name and "Help"
        bool shown = false;          ///< Switches only: whether what it controls starts shown
    };

    /// The items of `tool`'s menu, left to right; none for a tool that acts at once.
    [[nodiscard]] std::span<const ToolMenuItem> tool_menu(ParkTool tool);

    /// Whether `item` is a switch of the view controls.
    [[nodiscard]] bool is_switch(ToolItem item);

} // namespace rct3::ui
