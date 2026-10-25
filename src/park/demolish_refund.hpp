// SPDX-License-Identifier: GPL-3.0-or-later
//
// What pulling a ride or a shop down gives back: all it cost while it has never opened, less once guests have
// been on it.
#pragma once

#include <cstdint>

namespace rct3::park
{

    /// Of `value` (what it cost to build, hundredths), what pulling it down pays back.
    [[nodiscard]] constexpr std::int64_t demolish_refund(std::int64_t value, bool ever_opened)
    {
        // Taken from OpenRCT2 (GPL-3.0), src/openrct2/actions/track/TrackRemoveAction.cpp, TrackRemoveAction::Query:
        // 70% back for a ride that has been opened, all of it for one never opened.
        return ever_opened ? value * 7 / 10 : value;
    }

} // namespace rct3::park
