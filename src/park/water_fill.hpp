// SPDX-License-Identifier: GPL-3.0-or-later
//
// Filling a lake. Water poured on a tile up to a level spreads like a flood: every tile with a corner below
// the level that can be reached from it, crossing from tile to tile where both sides of the shared edge have a
// corner below the level. Each such tile holds water in its triangles that dip below the level, as the saved
// water bodies keep it.
//
// The water tool pours with a click: up to the height clicked, rounded up to a whole unit, taking in the lakes
// the flood reaches. Clicking a lake takes it away, with the waterfalls pouring from or into it. Shaping the
// ground under or beside a lake takes it away, rather than leaving water the new ground cannot hold.
#pragma once

#include "save/model/world.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// The water body filled from tile `col`, `row` to `level`. Its triangles are empty when the tile has no
    /// corner below the level. Nothing when the flood reaches a tile `allowed` refuses.
    [[nodiscard]] std::optional<save::WaterBody> fill_water(const save::TerrainGrid& grid, save::Tile tile, float level, const std::function<bool(save::Tile)>& allowed);

    /// The park's `water` after pouring on `tile` up to `level` rounded up to a whole unit: one new lake that
    /// takes in the lakes it reaches. `water` as it was when that holds no water; nothing when the flood reaches a
    /// tile `allowed` refuses or a lake standing higher.
    [[nodiscard]] std::optional<save::WaterLayer> pour_water(const save::WaterLayer& water, const save::TerrainGrid& grid, save::Tile tile, float level, const std::function<bool(save::Tile)>& allowed);

    /// Takes the lake holding `tile` out of `water`, and the `falls` pouring from or into it, putting back in `grid`
    /// the ground their spillways were cut from; whether there was one.
    bool remove_lake(save::WaterLayer& water, std::vector<save::Waterfall>& falls, save::TerrainGrid& grid, save::Tile tile);

    /// After the ground moved from `was` to `grid`: takes out of `water` every lake on a tile that moved, or beside
    /// one that now dips below its level, and out of `falls` every waterfall with an end on or beside a tile that
    /// moved, with its lakes, and those pouring from or into a lake taken. Whether any went.
    /// shortcut: the whole lake goes, where it could keep the water the new ground still holds (drying what rose
    /// out of it, or sinking to the highest level that does not leak); upgrade when players miss lakes they
    /// reshaped the banks of.
    bool drain_moved(save::WaterLayer& water, std::vector<save::Waterfall>& falls, const save::TerrainGrid& was, const save::TerrainGrid& grid);
    /// Whether `body` holds water on tile `col`, `row`.
    [[nodiscard]] bool holds(const save::WaterBody& body, save::Tile tile);

    /// Whether any of `bodies` holds water on `tile`.
    [[nodiscard]] bool holds_water(std::span<const save::WaterBody> bodies, save::Tile tile);

    /// The whole height an item floating on the water over `tile` stands at, its surface rounded up, or
    /// nothing on dry land.
    [[nodiscard]] std::optional<std::int32_t> water_surface(std::span<const save::WaterBody> bodies, save::Tile tile);

} // namespace rct3::park
