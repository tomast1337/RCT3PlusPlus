// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rides of two tracks side by side, which run together and apart: each of their pieces belongs to the left
// track or the right one (PieceRecord::side). A joined piece is one half of a pair laid together, on the same
// tiles and the same way, one half on each track; its record names the other half. The rest are laid on one
// track alone. Each track is built as stretches of its own.
//
// The construction panel's split button says where the next piece goes and of which kind, in four states the
// button steps through in turn: joined pieces from the left track's open end, the left track alone, joined
// pieces from the right track's open end, the right track alone. A state whose track is closed already is
// stepped over.
#pragma once

#include "park/track_pieces.hpp"

#include <array>
#include <cstdint>

namespace rct3::park
{

    enum class SplitState : std::uint8_t
    {
        None, ///< The ride has one track
        JoinedLeft,
        LeftAlone,
        JoinedRight,
        RightAlone,
    };

    /// The track a state builds on: 1 the left, 2 the right; 0 for None.
    [[nodiscard]] std::uint32_t split_side(SplitState state);

    /// Whether a state lays joined pieces.
    [[nodiscard]] bool split_joined(SplitState state);

    /// The state laying pieces of the kind of `piece`, on its track.
    [[nodiscard]] SplitState split_of(const PieceRecord& piece);

    /// The state the button moves `now` on to: the next in turn whose track is not `closed` (the left's first),
    /// only one laying the same kind of pieces when `same_kind`. `now` when there is none.
    [[nodiscard]] SplitState next_split(SplitState now, std::array<bool, 2> closed, bool same_kind);

} // namespace rct3::park
