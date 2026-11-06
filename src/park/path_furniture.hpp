// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the things guests use on paths stand: benches, bins, lamps and signs. An item's positioning says it
// goes on a path tile, against one of its sides or in its middle, and it may go nowhere else. Against a side,
// a bench or a bin takes the side nearest the pointer that no path joins, so guests sit facing the path and
// walkers pass; in a plaza, where every side is joined, it takes the nearest. A sign that stands where two paths
// join takes the nearest joined side. Where no side suits, as in the middle of a plaza or beside the next lane
// of a winding queue, it shows refused. One item stands against each side of a tile, and one in its middle.
// An item's data may also keep it off queues, or on queues only.
//
// A raised path that slopes takes nothing in its middle, and an item kept to level paths only at the slope's
// low or high end, where the side rule above allows it there; elsewhere on the slope it shows refused.
//
// On a raised path an item stands at the deck's height, stored as an absolute height; on the ground it follows
// the land, as other scenery does.
#pragma once

#include "park/object_mesh.hpp"
#include "park/object_placement.hpp"
#include "park/turns.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <glm/vec3.hpp>
#include <optional>
#include <span>

namespace rct3::park
{

    /// Whether an item positioned `positioning` stands on a path tile.
    [[nodiscard]] constexpr bool on_paths(Positioning positioning) noexcept
    {
        using enum Positioning;
        return positioning == PathSide || positioning == PathSideOuter || positioning == PathCentre || positioning == PathSideJoin;
    }

    struct FurnitureSpot
    {
        save::Tile tile;
        ModelTurn turn;
        std::int32_t height = 0; ///< Whole units; the deck's on a raised path, the land's middle on the ground
        bool raised = false;
        bool refused = false; ///< On a path, but not where the item may stand: no side suits, or its rules forbid the path
    };

    /// Where an item positioned `positioning` goes for the pointer at world point `hit`: on the path tile under it
    /// whose height is nearest, against the side chosen as above, or in the middle turned `turn`. Nothing off the
    /// paths. `rules` are the item's ObjectModel::path_rules.
    [[nodiscard]] std::optional<FurnitureSpot> furniture_spot(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, glm::vec3 hit, Positioning positioning, ModelTurn turn, PathRules rules = { });

    /// Whether another shown item of `objects` already stands where `item`, positioned `positioning`, would: in
    /// the middle of its tile, or against the same side, at about its height.
    [[nodiscard]] bool furniture_taken(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::PlacedObject& item, Positioning positioning);

} // namespace rct3::park
