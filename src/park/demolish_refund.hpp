// SPDX-License-Identifier: GPL-3.0-or-later
//
// What pulling a ride or a shop down gives back: all it cost while it has never opened, less once guests have
// been on it.
#pragma once

#include "units/units.hpp"

namespace rct3::park
{

    /// Of `value` (what it cost to build), what pulling it down pays back.
    [[nodiscard]] constexpr units::Money demolish_refund(units::Money value, bool ever_opened)
    {
        // Taken from OpenRCT2 (GPL-3.0), src/openrct2/actions/track/TrackRemoveAction.cpp, TrackRemoveAction::Query:
        // 70% back for a ride that has been opened, all of it for one never opened.
        return ever_opened ? units::Money { value.value * 7 / 10 } : value;
    }

} // namespace rct3::park
