// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/footpath_builder.hpp"

#include "park/ground_paths.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace rct3::park
{

    namespace
    {

        constexpr std::uint8_t kQuarterTurns = 4;

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

    LayProblem lay_problem(const FootpathBuilder& builder, const save::TerrainGrid& grid, bool queue)
    {
        if (queue)
            return LayProblem::Queue;
        if (builder.slope != PathSlope::Flat)
            return LayProblem::Raised;
        const auto spot = next_spot(builder);
        if (!spot)
            return LayProblem::NoStart;
        if (!grid.contains(spot->col, spot->row))
            return LayProblem::OffMap;
        if (grid.at(spot->col, spot->row).ownership() != save::Ownership::Owned)
            return LayProblem::NotOwned;
        return LayProblem::None;
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

    bool lay_ground_path(FootpathBuilder& builder, std::vector<save::PathTile>& paths, std::string_view style, std::uint64_t id)
    {
        const auto spot = next_spot(builder);
        if (!spot)
            return false;
        builder.on_slope = false;
        const bool there = ground_path_at(paths, *spot).has_value();
        builder.trail.push_back({ *spot, !there });
        if (there)
            return false;
        paths.push_back(ground_path(*spot, style, id));
        return true;
    }

    bool remove_last(FootpathBuilder& builder, std::vector<save::PathTile>& paths)
    {
        if (builder.trail.empty())
            return false;
        const TrailStep last = builder.trail.back();
        builder.trail.pop_back();
        if (!last.laid)
            return false;
        const auto at = ground_path_at(paths, last.spot);
        if (!at)
            return false;
        paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(*at));
        return true;
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
