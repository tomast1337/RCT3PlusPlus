// SPDX-License-Identifier: GPL-3.0-or-later
//
// Making a waterfall between two lakes. The water pours from the higher lake over a straight edge of it,
// square to the map's grid, and lands in the lower lake straight out from that edge. Of every edge tile of
// the higher lake with the lower lake straight out from it, the one with the shortest way across wins, then the
// one nearest where the player picked the higher lake. The edge then runs on to each side along the lake's
// border, a few tiles at most. Every tile the water pours over or across must be land the park owns outright.
//
// What is refused is named by the game's text keys, so the player reads the game's own words.
#pragma once

#include "save/model/world.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::park
{

    inline constexpr std::string_view kWaterfallSameWater = "Waterfall_CreationFailure_SameWaterBodies";
    inline constexpr std::string_view kWaterfallTooLittleDrop = "Waterfall_CreationFailure_WaterHeightDiffTooSmall";
    inline constexpr std::string_view kWaterfallTooFarApart = "Waterfall_CreationFailure_WaterTooFarApart";
    inline constexpr std::string_view kWaterfallNotOwned = "Waterfall_CreationFailure_LandNotOwned";
    inline constexpr std::string_view kWaterfallDeleteNotOwned = "Waterfall_DeletionFailure_LandNotOwned";

    /// The waterfall joining lakes `first` and `second` of `bodies` (in either order), `near` the tile the
    /// player picked the higher one on; or the text key of why there can be none.
    [[nodiscard]] std::expected<save::Waterfall, std::string_view> make_waterfall(const save::TerrainGrid& grid, std::span<const save::WaterBody> bodies, std::size_t first, std::size_t second, save::Tile near);

    /// Of `falls`, the one whose water passes over the ground at `x`, `z` (world units), seen from above.
    [[nodiscard]] std::optional<std::size_t> waterfall_at(std::span<const save::Waterfall> falls, float x, float z);

} // namespace rct3::park
