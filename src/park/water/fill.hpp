// SPDX-License-Identifier: GPL-3.0-or-later
//
// Filling a lake. Water poured on a tile up to a level spreads like a flood: every tile with a corner below
// the level that can be reached from it, crossing from tile to tile where both sides of the shared edge have a
// corner below the level. Each such tile holds water in its triangles that dip below the level, as the saved
// water bodies keep it.
//
// The water tool pours with a click: up to the height clicked, rounded up to a whole unit, taking in the lakes
// the flood reaches. Clicking a lake takes it away, with the waterfalls pouring from or into it. Shaping the
// ground under or beside a lake leaves it the water the new ground still holds (settle_water).
#pragma once

#include "save/model/world.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park::water
{

    /// The water body filled from tile `col`, `row` to `level`. Its triangles are empty when the tile has no
    /// corner below the level. Nothing when the flood reaches a tile `allowed` refuses.
    [[nodiscard]] std::optional<save::model::WaterBody> fill_water(const save::model::TerrainGrid& grid, save::model::Tile tile, float level, const std::function<bool(save::model::Tile)>& allowed);

    /// The park's `water` after pouring on `tile` up to `level` rounded up to a whole unit: one new lake that
    /// takes in the lakes it reaches. `water` as it was when that holds no water; nothing when the flood reaches a
    /// tile `allowed` refuses or a lake standing higher.
    [[nodiscard]] std::optional<save::model::WaterLayer> pour_water(const save::model::WaterLayer& water, const save::model::TerrainGrid& grid, save::model::Tile tile, float level, const std::function<bool(save::model::Tile)>& allowed);

    /// Takes the lake holding `tile` out of `water`, and the `falls` pouring from or into it, putting back in `grid`
    /// the ground their spillways were cut from; whether there was one.
    bool remove_lake(save::model::WaterLayer& water, std::vector<save::model::Waterfall>& falls, save::model::TerrainGrid& grid, save::model::Tile tile);

    /// Of `falls`, those pouring from or into a lake that `now` no longer holds as `was` did: one whose level
    /// changed, or that another lake took in.
    [[nodiscard]] std::vector<std::size_t> flooded_falls(const save::model::WaterLayer& was, const save::model::WaterLayer& now, std::span<const save::model::Waterfall> falls, const save::model::TerrainGrid& grid);

    /// After the ground moved from `was` to `grid`: settles every lake on a tile that moved, or beside one that now
    /// dips below its level. Such a lake keeps its level where the ground still holds it: tiles that rose out of it
    /// dry, ground dug beside it floods, and ground risen through its middle splits it in two. Where the ground
    /// that moved lets it run out onto ground that was dry, it sinks, a whole unit at a time, to the highest level
    /// that runs out nowhere, and goes only when there is none. Takes out of `falls` every waterfall with an end on
    /// or beside a tile that moved, and those whose lake at either end went or sank. Whether anything changed.
    bool settle_water(save::model::WaterLayer& water, std::vector<save::model::Waterfall>& falls, const save::model::TerrainGrid& was, const save::model::TerrainGrid& grid);
    /// Whether `body` holds water on tile `col`, `row`.
    [[nodiscard]] bool holds(const save::model::WaterBody& body, save::model::Tile tile);

    /// Whether any of `bodies` holds water on `tile`.
    [[nodiscard]] bool holds_water(std::span<const save::model::WaterBody> bodies, save::model::Tile tile);

    /// The whole height an item floating on the water over `tile` stands at, its surface rounded up, or
    /// nothing on dry land.
    [[nodiscard]] std::optional<std::int32_t> water_surface(std::span<const save::model::WaterBody> bodies, save::model::Tile tile);

} // namespace rct3::park::water
