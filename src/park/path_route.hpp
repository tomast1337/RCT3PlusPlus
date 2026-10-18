// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finding a path route from where a trail stands to a tile picked further off, laid as the path builder would
// lay it piece by piece (footpath_builder.hpp), so every rule the builder keeps holds along the route.
//
// The search walks the pieces the builder could lay next, from the trail's end: each a heading and a slope.
// As with the builder's controls, the heading turns only after a flat piece, never straight back. A piece is
// taken only where the builder would lay it (owned land, not into the ground unless cutting is on, clear of
// other paths). The route ends with a piece on the goal tile that lies on its ground or is the same piece as a
// path already there.
//
// The cheapest route wins: each piece costs its price, as laying it would (raised pieces more the higher they
// stand), plus kRouteStepCost so shorter routes win among equally priced ones, kRouteTurnCost for each turn so
// routes run straight, kRouteSlopeCost for each sloping piece so routes keep level where they can, and
// kRouteCutCost for each unit of ground cut away under each piece. Tunnel pieces cut nothing, so where the
// ground stands high a route goes under it rather than cut a deep trench. With cutting off, a route goes over
// or round a hill, or through it where it can reach a tunnel without cutting; with it on, it may cut its way
// down to a tunnel or through.
#pragma once

#include "park/footpath_builder.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// Our own values, in hundredths of the currency unit.
    inline constexpr std::int64_t kRouteStepCost = 100;
    inline constexpr std::int64_t kRouteTurnCost = 50;
    inline constexpr std::int64_t kRouteSlopeCost = 100;
    inline constexpr std::int64_t kRouteCutCost = 200;
    /// Pieces looked at before a search gives up. Our own value.
    inline constexpr std::size_t kRouteSearchLimit = 60000;

    /// A piece of a route: how the builder is set to lay it, and the piece it lays.
    struct RouteStep
    {
        std::uint8_t heading = 0;
        PathSlope slope = PathSlope::Flat;
        NextPiece piece;
    };

    /// The cheapest route from where `from` would lay its next piece to `goal`, laid with `from`'s cutting and
    /// queue settings among `paths`, whose ground pieces cost `ground_cost`; nothing when there is none within
    /// the search limit. An empty route when the trail already ends on the goal.
    [[nodiscard]] std::optional<std::vector<RouteStep>> find_route(const FootpathBuilder& from, PathSpot goal, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::int32_t ground_cost);

    /// What laying `route` costs, as the pieces' prices add up (walking over a path already there is free).
    [[nodiscard]] std::int64_t route_price(std::span<const RouteStep> route, const FootpathBuilder& from, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::int32_t ground_cost);

} // namespace rct3::park
