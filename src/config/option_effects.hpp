// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which game options the program acts on so far. The others are kept and saved like any option, but nothing
// reads them yet: the options window shows their rows greyed so its pages still read as the game's do.
//
// Some act only at a point: the scenery and sky choices when a park loads, the menu music's when it next
// changes track.
#pragma once

#include "config/game_options.hpp"

#include <bitset>
#include <span>

namespace rct3::config
{

    /// The options something in the program reads, the interface's own included.
    [[nodiscard]] std::span<const GameOption> acted_on_options();

    /// Every option not in acted_on_options().
    [[nodiscard]] std::bitset<kGameOptionCount> inert_options();

} // namespace rct3::config
