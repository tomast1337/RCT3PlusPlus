// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/footpath_builder.hpp"

#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/path_decks.hpp"
#include "park/queue_fences.hpp"
#include "park/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace rct3::park
{

    namespace
    {

        constexpr std::uint8_t kQuarterTurns = 4;

        /// Where a trail picked on `spot` starts: on flat ground, else just above the tile's highest corner.
        std::int32_t ground_start(const save::TerrainGrid& grid, PathSpot spot)
        {
            const save::TerrainCell& cell = grid.at(spot.col, spot.row);
            if (cell.max_height() - cell.min_height() <= kFlat)
                return static_cast<std::int32_t>(std::lround(cell.min_height()));
            return static_cast<std::int32_t>(std::ceil(cell.max_height() - kPathMeets));
        }

        /// The lowest and highest heights `path` takes on its tile.
        std::pair<float, float> span_of(const save::PathTile& path, const save::TerrainGrid& grid)
        {
            if (path.elevation && !is_ground_queue(path))
            {
                const save::PathElevation& e = *path.elevation;
                const float rise = path_rise(e.slope);
                const float low = static_cast<float>(e.quantised_height) - (slope_falls(e.slope) ? rise : 0);
                return { low, low + rise };
            }
            if (path.elevation)
                return { static_cast<float>(path.elevation->quantised_height), static_cast<float>(path.elevation->quantised_height) };
            const save::TerrainCell& cell = grid.at(path.col, path.row);
            return { cell.min_height(), cell.max_height() };
        }

        /// Calls `visit(corner, height)` for each corner of `piece`'s tile with the piece's height over it: the
        /// corners on the edge it leaves by take its end, the others its start.
        template<typename Visit> void piece_corners(const NextPiece& piece, std::uint8_t heading, Visit visit)
        {
            const auto exit = edge_corners(heading);
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                const bool leaving = corner == exit[0] || corner == exit[1];
                visit(corner, static_cast<float>(leaving ? piece.end : piece.start));
            }
        }

    } // namespace

    PathSpot step(PathSpot from, std::uint8_t heading) noexcept
    {
        switch (heading % kQuarterTurns)
        {
            case 0:
                return { from.col, from.row + 1 };
            case 1:
                return { from.col + 1, from.row };
            case 2:
                return { from.col, from.row - 1 };
            default:
                return { from.col - 1, from.row };
        }
    }

    void start_at(FootpathBuilder& builder, PathSpot spot)
    {
        builder.start = spot;
        builder.trail.clear();
        builder.on_slope = false;
    }

    std::optional<PathSpot> next_spot(const FootpathBuilder& builder)
    {
        if (!builder.trail.empty())
            return step(builder.trail.back().spot, builder.heading);
        return builder.start;
    }

    std::optional<NextPiece> next_piece(const FootpathBuilder& builder, const save::TerrainGrid& grid)
    {
        const auto spot = next_spot(builder);
        if (!spot || !grid.contains(spot->col, spot->row))
            return std::nullopt;
        NextPiece piece;
        piece.spot = *spot;
        piece.start = builder.trail.empty() ? ground_start(grid, *spot) : builder.trail.back().height;
        piece.end = piece.start + static_cast<std::int32_t>(slope_rise(builder.slope));
        bool level = builder.slope == PathSlope::Flat;
        const auto& corners = grid.at(spot->col, spot->row).corners;
        piece_corners(piece, builder.heading, [&](std::size_t corner, float height) {
            piece.cut = piece.cut || corners[corner] > height + kPathMeets;
            // Once cut, a corner in the way stands at the piece.
            const float ground = builder.cut_into_ground ? std::min(corners[corner], height) : corners[corner];
            level = level && std::abs(ground - height) <= kFlat;
        });
        piece.ground = level && (builder.cut_into_ground || !piece.cut);
        return piece;
    }

    LayProblem lay_problem(const FootpathBuilder& builder, const save::TerrainGrid& grid, std::span<const save::PathTile> paths)
    {
        const auto spot = next_spot(builder);
        if (!spot)
            return LayProblem::NoStart;
        const auto piece = next_piece(builder, grid);
        if (!piece)
            return LayProblem::OffMap;
        if (grid.at(spot->col, spot->row).ownership() != save::Ownership::Owned)
            return LayProblem::NotOwned;
        if (piece->cut && !builder.cut_into_ground)
            return LayProblem::Underground;
        if (same_piece_at(paths, *piece, builder))
            return LayProblem::None;
        const auto low = static_cast<float>(std::min(piece->start, piece->end));
        const auto high = static_cast<float>(std::max(piece->start, piece->end));
        for (const save::PathTile& p : paths)
        {
            if (p.col != piece->spot.col || p.row != piece->spot.row)
                continue;
            const auto [other_low, other_high] = span_of(p, grid);
            if (other_low < high + kPathHeadroom && low < other_high + kPathHeadroom)
                return LayProblem::Occupied;
        }
        return LayProblem::None;
    }

    void cut_down(save::TerrainGrid& grid, const NextPiece& piece, std::uint8_t heading)
    {
        if (!grid.contains(piece.spot.col, piece.spot.row))
            return;
        auto& corners = grid.cells[static_cast<std::size_t>(piece.spot.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(piece.spot.col)].corners;
        piece_corners(piece, heading, [&](std::size_t corner, float height) { corners[corner] = std::min(corners[corner], height); });
    }

    float height_above_ground(const NextPiece& piece, const save::TerrainGrid& grid)
    {
        if (!grid.contains(piece.spot.col, piece.spot.row))
            return 0;
        const auto& corners = grid.at(piece.spot.col, piece.spot.row).corners;
        const float ground = (corners[0] + corners[1] + corners[2] + corners[3]) / 4;
        return static_cast<float>(piece.start + piece.end) / 2 - ground;
    }

    std::optional<std::size_t> ground_path_at(std::span<const save::PathTile> paths, PathSpot spot)
    {
        const auto found = std::ranges::find_if(paths, [&](const save::PathTile& p) { return p.kind == save::PathKind::Ground && p.col == spot.col && p.row == spot.row; });
        if (found == paths.end())
            return std::nullopt;
        return static_cast<std::size_t>(found - paths.begin());
    }

    save::PathTile ground_path(PathSpot spot, std::string_view style, std::uint64_t id)
    {
        save::PathTile p;
        p.id = id;
        p.kind = save::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(spot.col);
        p.row = static_cast<std::uint8_t>(spot.row);
        p.surface_name = std::string(style);
        return p;
    }

    std::optional<std::size_t> same_piece_at(std::span<const save::PathTile> paths, const NextPiece& piece, const FootpathBuilder& builder)
    {
        if (piece.ground && !builder.queue)
            return ground_path_at(paths, piece.spot);
        const std::uint8_t stored = piece.ground ? save::kSlopeOnGround : stored_slope(builder.slope);
        const auto found = std::ranges::find_if(paths, [&](const save::PathTile& p) {
            if (p.col != piece.spot.col || p.row != piece.spot.row || (p.kind == save::PathKind::Queue) != builder.queue || !p.elevation)
                return false;
            const save::PathElevation& e = *p.elevation;
            // A queue's direction is its line's business; any queue piece of the same shape is the same.
            return e.slope == stored && e.quantised_height == piece.start && (stored == 0 || stored == save::kSlopeOnGround || builder.queue || p.direction == stored_direction(builder.heading));
        });
        if (found == paths.end())
            return std::nullopt;
        return static_cast<std::size_t>(found - paths.begin());
    }

    bool lay_path(FootpathBuilder& builder, std::vector<save::PathTile>& paths, const NextPiece& piece, std::string_view style, std::uint64_t id, std::uint64_t deck)
    {
        builder.on_slope = builder.slope != PathSlope::Flat;
        const bool there = same_piece_at(paths, piece, builder).has_value();
        const std::uint64_t before = builder.trail.empty() ? 0 : builder.trail.back().laid;
        builder.trail.push_back({ piece.spot, there ? 0 : id, piece.end });
        if (there)
            return false;
        save::PathTile p = ground_path(piece.spot, style, id);
        if (builder.queue)
        {
            p.kind = save::PathKind::Queue;
            p.queue = save::PathQueueInfo { };
            p.queue->start_direction = stored_direction(builder.heading);
            p.queue->end_direction = stored_direction(static_cast<std::uint8_t>((builder.heading + 2) % kQuarterTurns));
            // The tile laid before now leads on to this one, on the same line.
            const auto last = std::ranges::find(paths, before, &save::PathTile::id);
            if (before != 0 && last != paths.end() && last->queue)
            {
                last->queue->start_direction = stored_direction(builder.heading);
                p.queue->queue_line_ref = last->queue->queue_line_ref;
            }
        }
        if (!piece.ground || builder.queue)
        {
            p.elevation = save::PathElevation { };
            p.elevation->quantised_height = piece.start;
        }
        if (piece.ground && builder.queue)
        {
            p.direction = 0;
            p.elevation->slope = save::kSlopeOnGround;
        }
        else if (!piece.ground)
        {
            if (!builder.queue)
                p.kind = save::PathKind::Flying;
            p.direction = stored_direction(builder.heading);
            p.elevation->slope = stored_slope(builder.slope);
            p.elevation->scenery_ref = deck;
        }
        paths.push_back(std::move(p));
        return true;
    }

    std::optional<save::PathTile> remove_last(FootpathBuilder& builder, std::vector<save::PathTile>& paths)
    {
        if (builder.trail.empty())
            return std::nullopt;
        const TrailStep last = builder.trail.back();
        builder.trail.pop_back();
        if (last.laid == 0)
            return std::nullopt;
        const auto at = std::ranges::find(paths, last.laid, &save::PathTile::id);
        if (at == paths.end())
            return std::nullopt;
        save::PathTile removed = std::move(*at);
        paths.erase(at);
        return removed;
    }

    bool can_turn(const FootpathBuilder& builder) noexcept
    {
        return !builder.on_slope;
    }

    bool picks_slope(PathControl control) noexcept
    {
        return control >= PathControl::SteepDown && control <= PathControl::SteepUp;
    }

    PathControl slope_control(PathSlope slope) noexcept
    {
        return static_cast<PathControl>(static_cast<std::uint8_t>(PathControl::SteepDown) + static_cast<std::uint8_t>(slope));
    }

    PathSlope control_slope(PathControl control) noexcept
    {
        if (!picks_slope(control))
            return PathSlope::Flat;
        return static_cast<PathSlope>(static_cast<std::uint8_t>(control) - static_cast<std::uint8_t>(PathControl::SteepDown));
    }

    PathStep press(FootpathBuilder& builder, PathControl control)
    {
        switch (control)
        {
            case PathControl::TurnLeft:
                if (can_turn(builder))
                    builder.heading = static_cast<std::uint8_t>((builder.heading + kQuarterTurns - 1) % kQuarterTurns);
                return PathStep::None;
            case PathControl::TurnRight:
                if (can_turn(builder))
                    builder.heading = static_cast<std::uint8_t>((builder.heading + 1) % kQuarterTurns);
                return PathStep::None;
            case PathControl::Straight:
                return PathStep::Lay;
            case PathControl::Remove:
                return PathStep::Remove;
            case PathControl::CutIntoGround:
                builder.cut_into_ground = !builder.cut_into_ground;
                return PathStep::None;
            default:
                break;
        }
        // A slope control: the slope already chosen lays another piece like the last.
        const PathSlope slope = control_slope(control);
        if (slope == builder.slope)
            return PathStep::Lay;
        builder.slope = slope;
        return PathStep::None;
    }

    std::uint8_t stored_slope(PathSlope slope) noexcept
    {
        // The codes parks store: flat, steep up, steep down, (3 is a queue on the ground), up, down.
        switch (slope)
        {
            case PathSlope::SteepUp:
                return 1;
            case PathSlope::SteepDown:
                return 2;
            case PathSlope::Up:
                return 4;
            case PathSlope::Down:
                return 5;
            case PathSlope::Flat:
                break;
        }
        return 0;
    }

    float slope_rise(PathSlope slope) noexcept
    {
        const float rise = path_rise(stored_slope(slope));
        return slope == PathSlope::Down || slope == PathSlope::SteepDown ? -rise : rise;
    }

    std::int64_t piece_cost(std::int32_t ground_cost, bool raised, float height) noexcept
    {
        if (!raised || height <= 0)
            return ground_cost;
        return ground_cost + kRaisedPieceCost * static_cast<std::int64_t>(std::ceil(height));
    }

} // namespace rct3::park
