// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finishing a track for the player: the pieces that carry it on from its open end round to where its first
// piece starts, so it closes into a circuit. The way is found by trying the plainest ways first: from the open
// end, each plain piece that fits is laid in thought, and the end that leaves nearest the goal, counting the
// way come so far and the straight steps still to go across, up and along, is carried on next. Only plain
// pieces are laid (no stations, chains or pieces picked by name), and only where each meets the last.
#pragma once

#include "park/track_pieces.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace rct3::park
{

    /// A piece the finished track lays: which and where.
    struct LaidPlacement
    {
        LaidPiece piece;
        save::PlacedObject object; ///< As place_after sets it
    };

    enum class CompletionFailure : std::uint8_t
    {
        NoWay,      ///< Every way was tried
        OutOfTries, ///< Gave up after `tries` ends
    };

    /// The pieces of `pieces` that carry a track on from `tip`, whose last piece is `last`, to `goal`: the
    /// open end a piece before its `first` piece would leave (start_of). Tries at most `tries` ends.
    // shortcut: the way may run through the ground, scenery and the track itself; check room for each piece
    // once building checks it for pieces laid by hand.
    [[nodiscard]] std::expected<std::vector<LaidPlacement>, CompletionFailure> complete_track(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, std::size_t tries);

} // namespace rct3::park
