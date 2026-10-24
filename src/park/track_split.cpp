// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_split.hpp"

namespace rct3::park
{

    std::uint32_t split_side(SplitState state)
    {
        switch (state)
        {
            case SplitState::JoinedLeft:
            case SplitState::LeftAlone:
                return 1;
            case SplitState::JoinedRight:
            case SplitState::RightAlone:
                return 2;
            case SplitState::None:
                break;
        }
        return 0;
    }

    bool split_joined(SplitState state)
    {
        return state == SplitState::JoinedLeft || state == SplitState::JoinedRight;
    }

    SplitState split_of(const PieceRecord& piece)
    {
        if (piece.side == 0)
            return SplitState::None;
        if (piece.partner.empty())
            return piece.side == 1 ? SplitState::LeftAlone : SplitState::RightAlone;
        return piece.side == 1 ? SplitState::JoinedLeft : SplitState::JoinedRight;
    }

    SplitState next_split(SplitState now, std::array<bool, 2> closed, bool same_kind)
    {
        if (now == SplitState::None)
            return now;
        constexpr int kStates = 4;
        for (int step = 1; step < kStates; ++step)
        {
            const auto next = static_cast<SplitState>((static_cast<int>(now) - 1 + step) % kStates + 1);
            if (!closed.at(split_side(next) - 1) && (!same_kind || split_joined(next) == split_joined(now)))
                return next;
        }
        return now;
    }

} // namespace rct3::park
