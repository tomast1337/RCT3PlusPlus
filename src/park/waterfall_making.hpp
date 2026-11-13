// SPDX-License-Identifier: GPL-3.0-or-later
//
// Making a waterfall between two lakes. The water pours from the higher lake over a straight edge of it,
// square to the map's grid, and lands in the lower lake straight out from that edge.
//
// The higher lake's border is a ring of straight runs of tile sides. The edge is the run the player's pick of
// the lower lake lies in front of: out from it, and between the two lines square to it at its ends. When the
// pick lies in front of no run that way, the lines at a run's ends are those halving the border's corners
// there instead, which takes in the ground off a corner. Of several such runs, the one wins whose middle is
// nearest the lower pick and which faces most the way from the higher pick to the lower one. The whole run is
// the edge, however long; a run of one tile is too narrow. The water pours between the edge's two end tiles,
// or over the middle of an edge of two. Every tile the water pours over or across must be land the park owns
// outright.
//
// Unless the lower lake lies right against the edge, the water first cuts a spillway into the bank: the row of
// tiles beyond the edge, as wide as it. The ground along the old edge sinks just under the higher lake's level,
// the spillway's far side rises to that level, so the lake spreads into it and the water pours from its far
// side. The edge's two ends keep their ground, so the end tiles slope up to the banks beside them; the ground
// on the way down to the lower lake sinks under the level where it stands higher, under the falling water. The
// waterfall keeps the ground as it was, to put it back when it goes.
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
    inline constexpr std::string_view kWaterfallEdgeTooNarrow = "Waterfall_CreationFailure_EdgeTooNarrow";
    inline constexpr std::string_view kWaterfallTooFarApart = "Waterfall_CreationFailure_WaterTooFarApart";
    inline constexpr std::string_view kWaterfallNotOwned = "Waterfall_CreationFailure_LandNotOwned";
    inline constexpr std::string_view kWaterfallDeleteNotOwned = "Waterfall_DeletionFailure_LandNotOwned";
    inline constexpr std::string_view kWaterfallLandUnsuitable = "Waterfall_CreationFailure_LandUnsuitable";

    inline constexpr float kSpillwayDepth = 0.25f; ///< Chosen by eye: how far under the lake's level the spillway's floor lies
    inline constexpr float kSpillwayRim = 0.01f;   ///< Chosen by eye: over the level at its far side, so the lake stops there

    /// The waterfall joining lakes `first` and `second` of `bodies` (in either order), picked by the player on
    /// tiles `at_first` and `at_second`, with the ground of its spillway as it is; or the text key of why there can
    /// be none. The ground is left as it is: cut_spillway cuts it.
    [[nodiscard]] std::expected<save::model::Waterfall, std::string_view> make_waterfall(const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> bodies, std::size_t first, std::size_t second, save::model::Tile at_first, save::model::Tile at_second);

    /// The ground of `grid` with the spillway of `fall` (as make_waterfall gave it) cut into it.
    void cut_spillway(save::model::TerrainGrid& grid, const save::model::Waterfall& fall);

    /// Puts the ground `fall`'s spillway was cut from back as it was.
    void restore_spillway(save::model::TerrainGrid& grid, const save::model::Waterfall& fall);

    /// Of `falls`, the one whose water passes over the ground at `x`, `z` (world units), seen from above.
    [[nodiscard]] std::optional<std::size_t> waterfall_at(std::span<const save::model::Waterfall> falls, float x, float z);

} // namespace rct3::park
