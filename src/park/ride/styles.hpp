// SPDX-License-Identifier: GPL-3.0-or-later
//
// The looks a ride's extras may take: each theme gives its rides a fence and an entrance and exit, and the player
// may give a ride another theme's, the fence and each door apart.
#pragma once

#include "core/types.hpp"
#include "park/build_catalogue.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park::ride
{

    /// A theme's fence, or its entrance and exit: the scenery items and the overlay that holds them.
    struct ExtraStyle
    {
        std::string title;               ///< The theme's, as shown to the player; empty for one with none
        std::string symbol, exit_symbol; ///< The fence, or the entrance and the exit
        std::string overlay;
    };

    struct RideStyles
    {
        std::vector<ExtraStyle> fences { };
        std::vector<ExtraStyle> doors { };
    };

    /// Every fence and pair of doors the build list's rides take, once each, in the order first met. Each is named
    /// after the theme whose own it is, where a ride of that theme has it from its theme's overlay.
    [[nodiscard]] RideStyles ride_styles(std::span<const BuildItem> items);

    enum class DoorEnd : u8
    {
        Entrance, ///< Also a fence's: ExtraStyle::symbol
        Exit,
    };

    /// The place in `styles` of the one that gives `symbol` in `overlay`, in any case and either kind of slash, as
    /// its `end`; none when no theme gives it.
    [[nodiscard]] std::optional<usize> style_of(std::span<const ExtraStyle> styles, std::string_view symbol, std::string_view overlay, DoorEnd end = DoorEnd::Entrance);

} // namespace rct3::park::ride
