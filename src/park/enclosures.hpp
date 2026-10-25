// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosures on the park's tiles (save::EnclosureMap): marking areas into them or out, and the fence
// round each. A fence stands on every edge between a tile of an enclosure and one that is not of it, one
// panel to an edge, inside the enclosure, leaning with the ground as the fence round the park does. Two
// enclosures that touch share one fence, the one of the enclosure listed first.
#pragma once

#include "park/boundary_fence.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace rct3::park
{

    /// The scenery item a fence of kind `fence` ("Wood") is built of, edge by edge, and the overlay it is in.
    /// shortcut: named as the install lays out its fences; read the fence's own record when a mod names its
    /// pieces otherwise.
    [[nodiscard]] std::pair<std::string, std::string> fence_piece(const std::string& fence);

    /// The fences of every enclosure in `map`, over `grid`. New objects take ids after `last_id`.
    /// shortcut: every edge takes a plain panel; the game opens gates where a ride's track crosses, add them
    /// when tracks may cross enclosures.
    [[nodiscard]] BoundaryFence enclosure_fences(const save::EnclosureMap& map, const save::TerrainGrid& grid, std::uint64_t last_id);

    /// Makes the tiles from `a` to `b` an enclosure fenced with `fence`. An area that takes in or touches
    /// enclosures joins them into one, the first of them, keeping its fence. A map with no grid yet takes
    /// `grid`'s size. Returns the enclosure's place in the map.
    std::int32_t enclose(save::EnclosureMap& map, const save::TerrainGrid& grid, save::Tile a, save::Tile b, const std::string& fence);

    /// Takes the tiles from `a` to `b` out of any enclosure. An enclosure left with no tiles goes.
    /// shortcut: an enclosure cut in two stays one, fenced round both halves; split it when animals choose
    /// which half they are in.
    void unenclose(save::EnclosureMap& map, save::Tile a, save::Tile b);

    /// The enclosure all `span` tiles from `corner` are in; -1 when they are not all in the same one.
    [[nodiscard]] std::int32_t enclosure_under(const save::EnclosureMap& map, save::Tile corner, std::array<std::int32_t, 2> span) noexcept;

    /// How many tiles enclosure `region` has.
    [[nodiscard]] std::int32_t enclosure_size(const save::EnclosureMap& map, std::int32_t region) noexcept;

} // namespace rct3::park
