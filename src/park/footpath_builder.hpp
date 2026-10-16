// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laying paths a piece at a time, as the path window's controls do. The player keeps a heading and a slope;
// each piece goes on from the end of the last one in that heading, at that slope. Three controls turn the
// heading or go straight on, five pick the slope, one takes the last piece away and one switches whether a
// piece may cut into the ground it meets.
//
// Going straight on lays the next piece. So does picking the slope already chosen: the slope controls double
// as "lay one more like that". No turning is allowed on a sloping piece, so the turns stay off until a flat
// piece is laid.
//
// The pieces laid so far form a trail: the next piece goes one tile on from the trail's last, in the heading,
// or on the tile the player picked when the trail is empty. Taking a piece away steps back along the trail; a
// path the trail only walked over stays, since this trail did not lay it.
// For now only flat pieces on the ground can be laid, on land the park owns; a tile that already has a path
// on the ground is walked over without laying another.
//
// A piece on the ground costs its style's price. A raised piece costs more the higher its middle is above the
// ground under it: kRaisedPieceCost more for each started unit of height.
#pragma once

#include "save/model/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// The slopes a piece can have, in the order the window shows them.
    enum class PathSlope : std::uint8_t
    {
        SteepDown, ///< Steps down
        Down,
        Flat,
        Up,
        SteepUp, ///< Steps up
    };
    inline constexpr std::size_t kPathSlopeCount = 5;

    /// The window's controls, in the order it lays them out.
    enum class PathControl : std::uint8_t
    {
        TurnLeft,
        Straight,
        TurnRight,
        SteepDown,
        Down,
        Flat,
        Up,
        SteepUp,
        Remove,
        CutIntoGround,
    };
    inline constexpr std::size_t kPathControlCount = 10;

    /// What a control asks of the park after it changed the builder.
    enum class PathStep : std::uint8_t
    {
        None,
        Lay,    ///< Lay the next piece
        Remove, ///< Take the last piece away
    };

    /// A tile of the park.
    struct PathSpot
    {
        int col = 0;
        int row = 0;
        friend bool operator==(const PathSpot&, const PathSpot&) = default;
    };

    /// A tile of the trail, and whether this trail laid the path on it or walked over one already there.
    struct TrailStep
    {
        PathSpot spot;
        bool laid = false;
        friend bool operator==(const TrailStep&, const TrailStep&) = default;
    };

    struct FootpathBuilder
    {
        /// Quarter turns clockwise as the park is drawn: 0 towards higher rows, 1 towards higher columns, 2
        /// towards lower rows, 3 towards lower columns.
        std::uint8_t heading = 0;
        PathSlope slope = PathSlope::Flat;
        bool cut_into_ground = false;
        bool on_slope = false;         ///< The last piece laid slopes, so the heading cannot change
        std::optional<PathSpot> start; ///< Where the trail began
        std::vector<TrailStep> trail;  ///< The tiles laid or walked over since, oldest first
    };

    /// Why the next piece cannot be laid.
    enum class LayProblem : std::uint8_t
    {
        None,
        NoStart,  ///< No tile picked yet
        OffMap,   ///< The next tile is beyond the park's ground
        NotOwned, ///< The park does not own the next tile
        Raised,   ///< A sloping piece, which would stand off the ground
        Queue,    ///< A queue style is chosen
    };

    /// Applies `control` to `builder` and says what the park should do.
    [[nodiscard]] PathStep press(FootpathBuilder& builder, PathControl control);

    /// The tile one step from `from` in `heading`.
    [[nodiscard]] PathSpot step(PathSpot from, std::uint8_t heading) noexcept;

    /// Begins a new trail at `spot`; what was laid stays.
    void start_at(FootpathBuilder& builder, PathSpot spot);

    /// Where the next piece goes; none before a tile is picked.
    [[nodiscard]] std::optional<PathSpot> next_spot(const FootpathBuilder& builder);

    /// Why the next piece cannot be laid on `grid`, for a queue style or a path style.
    [[nodiscard]] LayProblem lay_problem(const FootpathBuilder& builder, const save::TerrainGrid& grid, bool queue);

    /// Of `paths`, the path on the ground at `spot`.
    [[nodiscard]] std::optional<std::size_t> ground_path_at(std::span<const save::PathTile> paths, PathSpot spot);

    /// A path on the ground of `style` at `spot`, as a park keeps it.
    [[nodiscard]] save::PathTile ground_path(PathSpot spot, std::string_view style, std::uint64_t id);

    /// Lays the next piece as a path on the ground of `style` with id `id`, or walks over the path already
    /// there, and moves on. Returns whether a piece was added to `paths`. The caller checks lay_problem first.
    bool lay_ground_path(FootpathBuilder& builder, std::vector<save::PathTile>& paths, std::string_view style, std::uint64_t id);

    /// Steps back along the trail, taking its last piece away from `paths` when the trail laid it. Returns
    /// whether a path was removed.
    bool remove_last(FootpathBuilder& builder, std::vector<save::PathTile>& paths);

    /// Whether the turn controls can be used now.
    [[nodiscard]] bool can_turn(const FootpathBuilder& builder) noexcept;

    /// The slope control that picks `slope`, and the slope a control picks (Flat for those that pick none).
    [[nodiscard]] PathControl slope_control(PathSlope slope) noexcept;
    [[nodiscard]] bool picks_slope(PathControl control) noexcept;
    [[nodiscard]] PathSlope control_slope(PathControl control) noexcept;

    /// The slope as a park stores it on a raised path piece.
    [[nodiscard]] std::uint8_t stored_slope(PathSlope slope) noexcept;

    /// How far a piece of `slope` climbs from its start to its end, in world units; negative going down.
    [[nodiscard]] float slope_rise(PathSlope slope) noexcept;

    /// Added to a raised piece's price for each started unit of height of its middle above the ground, in
    /// hundredths of the currency unit.
    inline constexpr std::int64_t kRaisedPieceCost = 500;

    /// The price of a piece whose style costs `ground_cost` on the ground. `raised` false for a piece laid on
    /// the ground; otherwise `height` is how far its middle is above the ground under it, in world units.
    [[nodiscard]] std::int64_t piece_cost(std::int32_t ground_cost, bool raised, float height) noexcept;

} // namespace rct3::park
