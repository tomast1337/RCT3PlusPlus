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
//
// Each piece starts at the height the last one ended at; the first starts on the ground of its tile (above
// its highest corner when the tile is not flat). A flat piece that meets the flat ground of its tile lies on
// the ground; any other piece is raised, drawn by a deck object and held up by supports. A piece may not go
// into the ground: every corner of its tile must be at most as high as the piece where it crosses that
// corner's edge. With cutting on, the corners in the way are cut down to the piece instead, so a flat piece
// cut into a hill lies on the ground it made. A tile that already has the same piece is walked over without
// laying another; a tile with another path within kPathHeadroom of the piece's height is taken. Pieces go
// only on land the park owns.
//
// Queues are laid the same way, as a line of guests that flows back along the trail: each queue tile's end
// side faces the tile laid before it and its start side the way the trail goes on, so a queue begun beside a
// ride's entrance and laid away from it leads guests to the entrance. A queue tile on the ground covers its
// whole tile; a raised one is drawn by a deck object like a raised path.
//
// The next piece is shown with an arrow over its tile pointing the way the trail goes. Pressing on that tile
// and moving the pointer up or down before letting go steps the slope: each kSlopeDragStep up is one step
// steeper upwards (flat, ramp up, stairs up), each down one step downwards; letting go lays the piece.
//
// A piece with the ground of its whole tile at least kTunnelCover above its top is a tunnel piece: it goes
// under the ground without cutting it, so it is never refused for going into the ground, and it is kept
// whether cutting is on or off. Where a tunnel meets a slope or a cliff the ground opens round it. A tunnel is
// reached from a piece cut into the slope in front of it, or from below a cliff.
//
// A piece on the ground costs its style's price. A raised piece costs more the higher its middle is above the
// ground under it: kRaisedPieceCost more for each started unit of height. A tunnel piece costs
// kTunnelPieceCost more.
#pragma once

#include "park/terrain_mesh.hpp"
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

    /// A tile of the trail: the path this trail laid on it, or 0 when it walked over one already there.
    struct TrailStep
    {
        PathSpot spot;
        std::uint64_t laid = 0;
        std::int32_t height = 0; ///< Where the piece ends, in whole world units
        friend bool operator==(const TrailStep&, const TrailStep&) = default;
    };

    struct FootpathBuilder
    {
        /// Quarter turns clockwise as the park is drawn: 0 towards higher rows, 1 towards higher columns, 2
        /// towards lower rows, 3 towards lower columns.
        std::uint8_t heading = 0;
        PathSlope slope = PathSlope::Flat;
        bool cut_into_ground = false;
        bool queue = false;            ///< Lays queue tiles rather than paths
        bool on_slope = false;         ///< The last piece laid slopes, so the heading cannot change
        std::optional<PathSpot> start; ///< Where the trail began
        std::vector<TrailStep> trail;  ///< The tiles laid or walked over since, oldest first
    };

    /// Why the next piece cannot be laid.
    enum class LayProblem : std::uint8_t
    {
        None,
        NoStart,     ///< No tile picked yet
        OffMap,      ///< The next tile is beyond the park's ground
        NotOwned,    ///< The park does not own the next tile
        Underground, ///< The piece would go into the ground and cutting is off
        Occupied,    ///< Another path is in the way
    };

    /// Clearance kept between a piece and another path on its tile, in world units: room for guests to walk
    /// under a raised path.
    inline constexpr std::int32_t kPathHeadroom = 2;

    /// Ground over a piece's top, in world units, that makes it a tunnel piece. Fitted to the
    /// parks' underground paths, all of which have about this much or more over them.
    inline constexpr float kTunnelCover = 3;

    /// The next piece as it would be laid.
    struct NextPiece
    {
        PathSpot spot;
        std::int32_t start = 0; ///< Height it is entered at, in whole world units
        std::int32_t end = 0;   ///< Height it is left at
        bool ground = false;    ///< Lies on the ground; otherwise raised
        bool cut = false;       ///< Goes into the ground, so the ground has to be cut down first
        bool tunnel = false;    ///< Goes under the ground with room to walk: a tunnel piece
        friend bool operator==(const NextPiece&, const NextPiece&) = default;
    };

    /// Applies `control` to `builder` and says what the park should do.
    [[nodiscard]] PathStep press(FootpathBuilder& builder, PathControl control);

    /// The tile one step from `from` in `heading`.
    [[nodiscard]] PathSpot step(PathSpot from, std::uint8_t heading) noexcept;

    /// Begins a new trail at `spot`; what was laid stays.
    void start_at(FootpathBuilder& builder, PathSpot spot);

    /// Where the next piece goes; none before a tile is picked.
    [[nodiscard]] std::optional<PathSpot> next_spot(const FootpathBuilder& builder);

    /// The next piece on `grid`; none before a tile is picked or beyond the grid.
    [[nodiscard]] std::optional<NextPiece> next_piece(const FootpathBuilder& builder, const save::TerrainGrid& grid);

    /// Why the next piece cannot be laid on `grid` among `paths`.
    [[nodiscard]] LayProblem lay_problem(const FootpathBuilder& builder, const save::TerrainGrid& grid, std::span<const save::PathTile> paths);

    /// Lowers the corners of `piece`'s tile that stand above it, laid heading `heading`, down to it.
    void cut_down(save::TerrainGrid& grid, const NextPiece& piece, std::uint8_t heading);

    /// How far the middle of `piece` is above the ground of its tile, in world units.
    [[nodiscard]] float height_above_ground(const NextPiece& piece, const save::TerrainGrid& grid);

    /// Of `paths`, the path on the ground at `spot`.
    [[nodiscard]] std::optional<std::size_t> ground_path_at(std::span<const save::PathTile> paths, PathSpot spot);

    /// A path on the ground of `style` at `spot`, as a park keeps it.
    [[nodiscard]] save::PathTile ground_path(PathSpot spot, std::string_view style, std::uint64_t id);

    /// Of `paths`, one that is already `piece` as `builder` would lay it.
    [[nodiscard]] std::optional<std::size_t> same_piece_at(std::span<const save::PathTile> paths, const NextPiece& piece, const FootpathBuilder& builder);

    /// Lays `piece` (from next_piece; the caller checks lay_problem and cuts the ground first) as a path of
    /// `style` with id `id`, drawn by object `deck` when raised, or walks over the same piece already there,
    /// and moves on. Returns whether a path was added to `paths`.
    bool lay_path(FootpathBuilder& builder, std::vector<save::PathTile>& paths, const NextPiece& piece, std::string_view style, std::uint64_t id, std::uint64_t deck);

    /// Steps back along the trail, taking its last piece away from `paths` when the trail laid it. Returns the
    /// path removed.
    std::optional<save::PathTile> remove_last(FootpathBuilder& builder, std::vector<save::PathTile>& paths);

    /// Whether the turn controls can be used now.
    [[nodiscard]] bool can_turn(const FootpathBuilder& builder) noexcept;

    /// The slope control that picks `slope`, and the slope a control picks (Flat for those that pick none).
    [[nodiscard]] PathControl slope_control(PathSlope slope) noexcept;
    [[nodiscard]] bool picks_slope(PathControl control) noexcept;
    [[nodiscard]] PathSlope control_slope(PathControl control) noexcept;

    /// How far the pointer moves up or down, in interface units, for each step of slope while dragging on the
    /// next piece.
    inline constexpr float kSlopeDragStep = 24;

    /// The slope `steps` steps up from `from` (down when negative), from steps down to steps up.
    [[nodiscard]] PathSlope stepped_slope(PathSlope from, int steps) noexcept;

    /// A square over `piece`'s tile, at the piece's height, whose pictures' bottom edge (v = 1) lies on the edge
    /// the piece leaves by in `heading`: the arrow showing where the trail goes. One batch.
    [[nodiscard]] TerrainMesh heading_arrow(const NextPiece& piece, std::uint8_t heading, const save::TerrainGrid& grid);

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

    /// Added to a tunnel piece's price, in hundredths of the currency unit.
    inline constexpr std::int64_t kTunnelPieceCost = 1000;

    /// The price of `piece` on `grid` of a style that costs `ground_cost` on the ground.
    [[nodiscard]] std::int64_t piece_price(const NextPiece& piece, const save::TerrainGrid& grid, std::int32_t ground_cost);

} // namespace rct3::park
