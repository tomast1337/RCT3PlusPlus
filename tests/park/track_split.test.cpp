// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_split.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;

namespace
{
    PieceRecord piece(std::uint32_t side, std::string partner = "")
    {
        PieceRecord r;
        r.side = side;
        r.partner = std::move(partner);
        return r;
    }
} // namespace

TEST_CASE("a piece is laid in the split state of its track and kind")
{
    CHECK(split_of(piece(1, "Other")) == SplitState::JoinedLeft);
    CHECK(split_of(piece(1)) == SplitState::LeftAlone);
    CHECK(split_of(piece(2, "Other")) == SplitState::JoinedRight);
    CHECK(split_of(piece(2)) == SplitState::RightAlone);
    CHECK(split_of(piece(0)) == SplitState::None);
    CHECK(split_side(SplitState::LeftAlone) == 1);
    CHECK(split_joined(SplitState::JoinedRight));
}

TEST_CASE("the split button steps through the states in turn, over those whose track is closed")
{
    CHECK(next_split(SplitState::JoinedLeft, { false, false }, false) == SplitState::LeftAlone);
    CHECK(next_split(SplitState::RightAlone, { false, false }, false) == SplitState::JoinedLeft);
    // The left track closed: only the right's states are left.
    CHECK(next_split(SplitState::JoinedRight, { true, false }, false) == SplitState::RightAlone);
    CHECK(next_split(SplitState::RightAlone, { true, false }, false) == SplitState::JoinedRight);
    // With a piece picked to build over, only to the state laying the same kind.
    CHECK(next_split(SplitState::JoinedLeft, { false, false }, true) == SplitState::JoinedRight);
    CHECK(next_split(SplitState::LeftAlone, { false, true }, true) == SplitState::LeftAlone);
    CHECK(next_split(SplitState::None, { false, false }, false) == SplitState::None);
}
