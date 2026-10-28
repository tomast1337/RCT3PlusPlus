// SPDX-License-Identifier: GPL-3.0-or-later
//
// What keeps the ground still under the land tools, and what moves with it.
//
// The ground under a flat ride, a stall or anything else whose item says it holds the ground never moves.
// A raised path lets the ground sink under it, but not rise into its deck; a path laid on the ground follows it
// while its tile stays near flat; a path underground never minds. Items that keep a height of their own (set
// to a fixed height, or raised off the ground) let the ground move until it would rise through the base of
// one of their solid squares. Everything else standing on the ground (trees, rocks, fences, walls, lamps)
// follows it: once the ground under it moved, it is seated again on the ground's new height.
//
// The tools ask tile by tile, with the tile's corners before and after: a tile refused keeps its corners and
// the rest of the stroke still moves.
#pragma once

#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rct3::park
{

    inline constexpr float kDeckRoom = 0.25f; ///< Chosen by eye: kept between the ground and a raised path's underside
    inline constexpr float kPavedTilt = 1.5f; ///< Chosen by eye: how far from flat a tile with a path on the ground may go
    inline constexpr float kShapeCost = 25;   ///< Price, in hundredths, of a height unit of one corner point moved

    /// What the land tools must leave be on each tile, by tile (row << 16 | column), gathered as a stroke starts.
    struct GroundHolds
    {
        std::unordered_set<std::uint32_t> held;         ///< Tiles under items that hold the ground
        SolidTiles raised;                              ///< Solid squares of items that keep a height of their own
        std::unordered_map<std::uint32_t, float> decks; ///< The lowest underside of the raised paths over a tile
        std::unordered_set<std::uint32_t> paved;        ///< Tiles with a path on the ground
        std::vector<std::size_t> followers;             ///< Items that stand on the ground and follow it, of the objects
    };

    [[nodiscard]] GroundHolds ground_holds(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// Whether the ground of `tile` may not go from corners `was` to `now`.
    [[nodiscard]] bool refuses(const GroundHolds& holds, save::Tile tile, const std::array<float, 4>& was, const std::array<float, 4>& now);

    /// A tile's ground after a cut or levelling.
    struct GroundCut
    {
        save::Tile tile { };
        std::array<float, 4> corners { };
    };

    /// Whether any of `cuts` may not be made: for `holds`, off land the park owns outright, or on a tile holding
    /// water of `bodies` or beside one.
    [[nodiscard]] bool cuts_refused(std::span<const GroundCut> cuts, const save::TerrainGrid& grid, const GroundHolds& holds, std::span<const save::WaterBody> bodies);

    /// What moving the ground to `cuts` costs, in hundredths.
    [[nodiscard]] std::int64_t cuts_cost(std::span<const GroundCut> cuts, const save::TerrainGrid& grid);

    /// Seats the followers with a tile from `first` to `last` under them on the ground again; whether there were any.
    bool seat_followers(std::span<save::PlacedObject> objects, const GroundHolds& holds, const ObjectModels& models, const save::TerrainGrid& grid, save::Tile first, save::Tile last);

} // namespace rct3::park
