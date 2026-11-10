// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_lines.hpp"

#include "park/footpath_builder.hpp"
#include "park/ground/paths.hpp"
#include "park/path/decks.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace rct3::park
{

    std::optional<EntranceBeside> entrance_beside(const save::model::RideModel& rides, save::model::Tile tile, std::optional<std::int32_t> height)
    {
        const auto [col, row] = tile;
        for (std::size_t r = 0; r < rides.rides.size(); ++r)
        {
            const std::vector<save::model::Station>& stations = rides.rides[r].stations;
            for (std::size_t s = 0; s < stations.size(); ++s)
            {
                const auto& at = stations[s].entrance_spot;
                if (!at || (height && std::abs(at->height - *height) > kEntranceReach))
                    continue;
                // shortcut: any side of the entrance's tile joins, the way it faces is not looked at; check it
                // once that is known.
                for (const Side side : kSides)
                {
                    const save::model::Tile beside = side.beyond({ col, row });
                    if (beside.col == at->col && beside.row == at->row)
                        return EntranceBeside { r, s, side };
                }
            }
        }
        return std::nullopt;
    }

    std::vector<std::size_t> tiles_behind(std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, std::size_t tile)
    {
        std::vector<std::size_t> out;
        std::size_t at = tile;
        while (true)
        {
            const save::model::PathTile& here = paths[at];
            const auto side = path::stored_heading(here.queue->end_direction);
            if (!side)
                return out; // It leads nowhere
            const auto edge = path::edge_height(here, grid, *side);
            const save::model::Tile next = side->beyond({ here.col, here.row });
            const auto found = std::ranges::find_if(paths, [&](const save::model::PathTile& p) {
                if (!p.queue || p.col != next.col || p.row != next.row || path::stored_heading(p.queue->start_direction) != side->opposite())
                    return false;
                const auto other = path::edge_height(p, grid, side->opposite());
                return edge && other && std::abs(*edge - *other) <= ground::kPathMeets;
            });
            if (found == paths.end())
                return out;
            const auto index = static_cast<std::size_t>(found - paths.begin());
            if (index == tile || std::ranges::contains(out, index))
                return out; // A loop
            out.push_back(index);
            at = index;
        }
    }

    bool turn_to_path(std::vector<save::model::PathTile>& paths, const save::model::TerrainGrid& grid, std::size_t tile)
    {
        save::model::PathTile& here = paths[tile];
        if (!here.queue)
            return false;
        // A tile beside, on `side`, whose edge meets this tile's there.
        const auto meets = [&](Side side, bool any_kind) {
            // A queue has edges only where its line passes: measure as if it came in on `side`.
            save::model::PathTile turned = here;
            turned.queue->start_direction = path::stored_direction(side);
            const auto edge = path::edge_height(turned, grid, side);
            const save::model::Tile next = side.beyond({ here.col, here.row });
            return edge && std::ranges::any_of(paths, [&](const save::model::PathTile& p) {
                       if (p.col != next.col || p.row != next.row || (!any_kind && p.queue))
                           return false;
                       const auto other = path::edge_height(p, grid, side.opposite());
                       return other && std::abs(*edge - *other) <= ground::kPathMeets;
                   });
        };
        const auto start = path::stored_heading(here.queue->start_direction);
        const auto end = path::stored_heading(here.queue->end_direction);
        if (start && meets(*start, true))
            return false;
        for (const Side side : kSides)
        {
            if (side != start && side != end && meets(side, false))
            {
                here.queue->start_direction = path::stored_direction(side);
                return true;
            }
        }
        return false;
    }

    namespace
    {
        /// Makes `paths[tile]` lead out by `side`, turning round the tiles behind it when they led the other way,
        /// and puts it and them on `line`.
        void lead_out(std::vector<save::model::PathTile>& paths, const save::model::TerrainGrid& grid, std::size_t tile, Side side, std::uint64_t line)
        {
            save::model::PathQueueInfo& here = *paths[tile].queue;
            const std::uint8_t toward = path::stored_direction(side);
            std::vector<std::size_t> joined;
            if (here.end_direction != toward)
            {
                // The queue so far flowed away from this tile: turn it round to lead here.
                const std::vector<std::size_t> behind = tiles_behind(paths, grid, tile);
                for (const std::size_t i : behind)
                    std::swap(paths[i].queue->start_direction, paths[i].queue->end_direction);
                here.start_direction = here.end_direction;
                here.end_direction = toward;
                joined = behind;
            }
            // The tiles behind, now leading here, plus any that already did.
            for (const std::size_t i : tiles_behind(paths, grid, tile))
            {
                if (!std::ranges::contains(joined, i))
                    joined.push_back(i);
            }
            joined.push_back(tile);
            for (const std::size_t i : joined)
                paths[i].queue->queue_line_ref = line;
        }

        /// Counts `line`'s tiles into the stations it leads to, and rails them with their ride's fence and colours.
        void take_ride(save::model::RideModel& rides, std::vector<save::model::PathTile>& paths, std::uint64_t line)
        {
            const auto on_line = [&](const save::model::PathTile& p) { return p.queue && p.queue->queue_line_ref == line; };
            for (save::model::Ride& ride : rides.rides)
            {
                for (save::model::Station& station : ride.stations)
                {
                    if (station.queue_line != line)
                        continue;
                    station.queue_tiles = static_cast<std::int32_t>(std::ranges::count_if(paths, on_line));
                    if (ride.fence_symbol.empty())
                        continue;
                    for (save::model::PathTile& p : paths | std::views::filter(on_line))
                    {
                        p.queue->fence_symbol = ride.fence_symbol;
                        p.queue->fence_overlay = ride.fence_overlay;
                        p.queue->fence_colours = { ride.fence_colours[0], ride.fence_colours[1], ride.fence_colours[2] };
                    }
                }
            }
        }
    } // namespace

    std::uint64_t join_entrance(save::model::RideModel& rides, std::vector<save::model::PathTile>& paths, const save::model::TerrainGrid& grid, std::size_t tile, const EntranceBeside& entrance, std::uint64_t new_line)
    {
        save::model::Station& station = rides.rides.at(entrance.ride).stations.at(entrance.station);
        if (station.queue_line == 0)
            station.queue_line = new_line;
        const std::uint64_t line = station.queue_line;
        lead_out(paths, grid, tile, entrance.side, line);
        take_ride(rides, paths, line);
        return line;
    }

    std::optional<std::uint64_t> join_line(save::model::RideModel& rides, std::vector<save::model::PathTile>& paths, const save::model::TerrainGrid& grid, std::size_t tile)
    {
        const save::model::PathTile& here = paths[tile];
        if (!here.queue)
            return std::nullopt;
        for (const Side side : kSides)
        {
            save::model::PathTile turned = here;
            turned.queue->end_direction = path::stored_direction(side);
            const auto edge = path::edge_height(turned, grid, side);
            const save::model::Tile next = side.beyond({ here.col, here.row });
            const auto head = std::ranges::find_if(paths, [&](const save::model::PathTile& p) {
                if (!p.queue || p.queue->queue_line_ref == 0 || p.queue->queue_line_ref == here.queue->queue_line_ref || p.col != next.col || p.row != next.row || path::stored_heading(p.queue->start_direction) != side.opposite())
                    return false;
                const auto other = path::edge_height(p, grid, side.opposite());
                return edge && other && std::abs(*edge - *other) <= ground::kPathMeets;
            });
            if (head == paths.end())
                continue;
            const std::uint64_t line = head->queue->queue_line_ref;
            lead_out(paths, grid, tile, side, line);
            take_ride(rides, paths, line);
            return line;
        }
        return std::nullopt;
    }

} // namespace rct3::park
