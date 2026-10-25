// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laying paths with the pointer, as the classic way of building them: no controls, only the park.
//
// Dragging over bare ground paints a path on the ground of every tile the pointer passes over, like a brush;
// tiles crossed between two looks at the pointer are filled in so a quick stroke leaves no gaps. A tile of
// the stroke is laid only where a single flat piece started on it would lie on the ground, as the path
// builder decides (footpath_builder.hpp): the park owns it, its ground is flat enough, and no path is in the
// way. Tiles that already have a path on the ground are kept and cost nothing; the rest are left out.
//
// Pointing at a tile that has a path picks that path's edge nearest the pointer (the tile split along its
// diagonals) and the tile beyond it: the next piece goes there, going away from the path, starting at the
// height the path meets that edge. Edges a path does not reach, such as the sides of a slope, offer nothing.
//
// A ride's entrance or exit is pointed at the same way, at its own height, so a raised ride's doors are reached
// in the air: the edge of the door's tile nearest the pointer gives the next piece's tile and heading.
#pragma once

#include "park/camera.hpp"
#include "park/footpath_builder.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// What a stroke over some tiles would do.
    struct DragPlan
    {
        std::vector<save::Tile> lay; ///< Tiles that get a new path
        std::size_t kept = 0;        ///< Tiles that already have one
        std::size_t refused = 0;     ///< Tiles that cannot have one
    };

    /// The tiles on the straight line from `from` to `to`, both included, each sharing a side with the one
    /// before it.
    [[nodiscard]] std::vector<save::Tile> brush_line(save::Tile from, save::Tile to);

    /// The tiles of the `size` by `size` square round `centre`; an even size reaches one tile further towards
    /// higher columns and rows than lower ones.
    [[nodiscard]] std::vector<save::Tile> brush_square(save::Tile centre, int size);

    /// What a stroke over `area` would do among `paths` and, when given, what else fills the tiles.
    [[nodiscard]] DragPlan plan_drag(std::span<const save::Tile> area, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::optional<const SolidTiles&> solids = std::nullopt);

    /// The side (as a heading) of a tile nearest a point on it `across` its width along columns and rows, each
    /// from 0 to 1.
    [[nodiscard]] std::uint8_t nearest_side(float across_col, float across_row) noexcept;

    /// A path under the pointer, and the edge of it the pointer is nearest.
    struct PathEdge
    {
        std::size_t path = 0;             ///< Of the paths
        std::optional<std::uint8_t> side; ///< None when the path does not reach that edge
    };

    /// The path `ray` meets first, if it meets one before the ground; a path is met at its middle height. With
    /// `through_ground`, as when the ground is drawn see-through, the ground hides no path off it, so paths in
    /// tunnels can be picked.
    [[nodiscard]] std::optional<PathEdge> path_under(const Ray& ray, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, bool through_ground = false);

    /// Has `builder` go on from `path`'s edge on `side`: the next piece on the tile beyond, heading away, at
    /// the height the path meets the edge. False, leaving the builder as it was, when the path does not reach
    /// that edge.
    bool start_from_edge(FootpathBuilder& builder, const save::PathTile& path, const save::TerrainGrid& grid, std::uint8_t side);

    /// A door under the pointer, and the edge of its tile the pointer is nearest.
    struct DoorEdge
    {
        std::size_t door = 0; ///< Of the doors
        std::uint8_t side = 0;
    };

    /// The door of `doors` that `ray` meets first, met flat across its tile at its height, if it meets one before
    /// the ground (or at all, `through_ground`).
    [[nodiscard]] std::optional<DoorEdge> door_under(const Ray& ray, std::span<const save::PlacementSpot> doors, const save::TerrainGrid& grid, bool through_ground = false);

    /// Has `builder` go on from `door`'s edge on `side`: the next piece on the tile beyond, heading away, at the
    /// door's height.
    void start_from_door(FootpathBuilder& builder, const save::PlacementSpot& door, std::uint8_t side);

} // namespace rct3::park
