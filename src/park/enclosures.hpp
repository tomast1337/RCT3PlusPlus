// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosures on the park's tiles (save::EnclosureMap): marking areas into them or out, and the fence
// round each. An enclosure is one connected area of tiles (joined by their sides) with one kind of fence:
// after every change, areas with the same fence that touch become one enclosure, and an enclosure cut in two
// becomes two. Areas with different fences stay apart, each fenced on its own side.
//
// A fence runs along every side of an enclosure's tile that faces a tile not of the enclosure, inside the
// enclosure, leaning with the ground as the fence round the park does. Each tile takes one piece for the sides
// it fences: a straight panel for one side (two for opposite sides), a corner piece for two sides that meet,
// an end piece for three and a closed pen for all four; a post fills each inside corner of the area.
#pragma once

#include "park/boundary_fence.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rct3::park
{

    /// The overlay the pieces of fence kind `fence` ("Wood") are in.
    /// shortcut: named as the install lays out its fences, as are the pieces (`fence` + "Edge", ...); read the
    /// fence's own record when a mod names them otherwise.
    [[nodiscard]] std::string fence_overlay(std::string_view fence);

    /// The fences of every enclosure in `map`, over `grid`. New objects take ids after `last_id`.
    /// shortcut: no gates; the gate piece goes where a ride's track crosses the fence, add it when tracks may
    /// run through enclosures.
    /// shortcut: always the plain look; the pieces also have a taller look for a step in the ground at the
    /// fence and worn looks for fences that wear out, add them with fence wear.
    [[nodiscard]] BoundaryFence enclosure_fences(const save::EnclosureMap& map, const save::TerrainGrid& grid, std::uint64_t last_id);

    /// How enclosure fence piece `piece` leans with the ground under it as it is now.
    [[nodiscard]] glm::vec2 fence_piece_lean(const save::TerrainGrid& grid, const save::PlacedObject& piece);

    /// Makes the tiles from `a` to `b` an enclosure fenced with `fence`: those `takeable` allows and in no
    /// enclosure yet; the rest are left as they are. A map with no grid yet takes `grid`'s size. Returns the
    /// place in the map of the enclosure the new tiles ended up in, -1 when none were taken.
    std::int32_t enclose(save::EnclosureMap& map, const save::TerrainGrid& grid, save::Tile a, save::Tile b, const std::string& fence, std::function_ref<bool(save::Tile)> takeable);

    /// Takes the tiles from `a` to `b` that `removable` allows out of their enclosures.
    void unenclose(save::EnclosureMap& map, save::Tile a, save::Tile b, std::function_ref<bool(save::Tile)> removable);

    /// What a kind of fence costs, a length at a time, in hundredths of the money unit.
    struct FencePrice
    {
        std::int64_t build = 0;
        std::int64_t refund = 0; ///< Paid back as a length comes down; negative
    };

    /// What going from enclosures `before` to `after` costs: the price of every length of fence put up, and
    /// the refund of every length taken down.
    [[nodiscard]] std::int64_t fence_cost(const save::EnclosureMap& before, const save::EnclosureMap& after, std::function_ref<FencePrice(std::string_view fence)> price);

    /// The enclosure all `span` tiles from `corner` are in; -1 when they are not all in the same one.
    [[nodiscard]] std::int32_t enclosure_under(const save::EnclosureMap& map, save::Tile corner, std::array<std::int32_t, 2> span) noexcept;

    /// How many tiles enclosure `region` has.
    [[nodiscard]] std::int32_t enclosure_size(const save::EnclosureMap& map, std::int32_t region) noexcept;

} // namespace rct3::park
