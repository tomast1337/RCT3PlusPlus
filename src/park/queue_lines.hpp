// SPDX-License-Identifier: GPL-3.0-or-later
//
// Joining queues to rides. A queue tile laid beside a ride's entrance joins the line of that entrance's
// station: its end side turns to face the entrance and every tile of the queue behind it takes the station's
// line, which is made when the station has none yet. A queue laid towards the entrance flows the other way
// until it gets there, so the tiles behind are turned round to lead to it. A queue laid on from the open end of a
// line joins that line the same way. A line's railings are its ride's fence, in the ride's fence colours.
//
// Two queue tiles are linked when one's end side meets the other's start side at the same height. A queue's
// open start side turns to a path beside it, so the line bends to meet the path as it is laid.
#pragma once

#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// A station entrance beside a tile.
    struct EntranceBeside
    {
        std::size_t ride = 0;    ///< Of the rides
        std::size_t station = 0; ///< Of the ride's stations
        std::uint8_t side = 0;   ///< Heading from the tile to the entrance, as the path builder counts them
        friend bool operator==(const EntranceBeside&, const EntranceBeside&) = default;
    };

    /// How far apart in height a queue tile and an entrance may be and still join, in world units.
    inline constexpr std::int32_t kEntranceReach = 1;

    /// The entrance beside tile (`col`, `row`) whose height is within kEntranceReach of `height`, if any;
    /// any height when `height` is empty.
    [[nodiscard]] std::optional<EntranceBeside> entrance_beside(const save::RideModel& rides, save::Tile tile, std::optional<std::int32_t> height);

    /// The queue tiles linked behind `paths[tile]` (following end sides), nearest first, not counting itself.
    [[nodiscard]] std::vector<std::size_t> tiles_behind(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::size_t tile);

    /// Turns the start side of queue tile `paths[tile]` towards a path (not a queue) beside it, when nothing meets
    /// that side yet and the path's edge meets the tile's there; the side guests come in by then opens onto the
    /// path. Returns whether it turned.
    bool turn_to_path(std::vector<save::PathTile>& paths, const save::TerrainGrid& grid, std::size_t tile);

    /// Joins queue tile `paths[tile]` to `entrance`, giving the station a line with id `new_line` when it has
    /// none, and rails the line with the ride's fence. Returns the line's id.
    std::uint64_t join_entrance(save::RideModel& rides, std::vector<save::PathTile>& paths, const save::TerrainGrid& grid, std::size_t tile, const EntranceBeside& entrance, std::uint64_t new_line);

    /// Joins queue tile `paths[tile]` to the line whose open start side faces it from a tile beside, as it joins an
    /// entrance: it leads into that line's head, and the tiles behind it follow. Returns the line's id, nothing
    /// when no line's head is beside it.
    std::optional<std::uint64_t> join_line(save::RideModel& rides, std::vector<save::PathTile>& paths, const save::TerrainGrid& grid, std::size_t tile);

} // namespace rct3::park
