// SPDX-License-Identifier: GPL-3.0-or-later
//
// The category every placeable item names in its description, telling what kind of thing it is. Only the
// values checked against the game's items have names here; the rest are kept as numbers.
#pragma once

#include <cstdint>

namespace rct3::park
{

    enum class ItemCategory : std::uint32_t
    {
        Tree = 0,
        Shrub = 2,
        Flowers = 3,
        Fence = 4, ///< Fences and queue railings, along a tile's edges
        Wall = 5,  ///< Building walls, arches and drapes
        SmallScenery = 7,
        MediumScenery = 8,
        Animatronic = 10,
        SupportColumn = 12, ///< The middle of a support column; the next five are its other parts
        SupportCap = 17,    ///< The last of the support parts
        TrackPiece = 18,
        PathPiece = 19,
        ParkEntrance = 20,
        Leftover = 21,   ///< Litter, sick and animal food lying about
        GuestSpawn = 22, ///< Where guests walk in from
        Ride = 23,
        RideEntrance = 24,
        RideExit = 25,
        KeepClear = 26, ///< The fence kept clear round a ride
        Stall = 27,
        RideEvent = 28,
        Fireworks = 29,
        LitterBin = 30,
        Bench = 31,
        Sign = 32,
        PathFeature = 33, ///< Information points, speakers, fountains and the like
        Floor = 34,       ///< Building floors, which roof what is under them
        Roof = 35,
        WallCorner = 36,
        Effect = 44, ///< Fire, mist and smoke
    };

    /// Whether guests stop to watch an item of `category`: animatronics, effects and path features.
    /// shortcut: path features take in information points and speakers too, which nothing tells apart yet;
    /// upgrade when the item's own data does.
    [[nodiscard]] constexpr bool is_sight(std::uint32_t category) noexcept
    {
        return category == static_cast<std::uint32_t>(ItemCategory::Animatronic) || category == static_cast<std::uint32_t>(ItemCategory::PathFeature) || category == static_cast<std::uint32_t>(ItemCategory::Effect);
    }

    /// Whether an item of `category` is a prop the player clicks on to look at by itself, rather than part of
    /// a path, a ride, the supports under them, or something lying about.
    [[nodiscard]] constexpr bool is_prop(std::uint32_t category) noexcept
    {
        const auto in = [&](ItemCategory low, ItemCategory high) { return category >= static_cast<std::uint32_t>(low) && category <= static_cast<std::uint32_t>(high); };
        return !in(ItemCategory::SupportColumn, ItemCategory::PathPiece) && !in(ItemCategory::Leftover, ItemCategory::Stall) && category != static_cast<std::uint32_t>(ItemCategory::Effect);
    }

} // namespace rct3::park
