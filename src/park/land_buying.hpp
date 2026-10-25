// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buying land for the park. A tile on sale is sold either outright or as building rights only, each at the
// park's price for its kind; buying it makes it the park's that way. Each tile is paid for on its own, so a
// brush that reaches more than the park can pay for buys what it can and leaves the rest on sale.
#pragma once

#include "save/model/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rct3::park
{

    /// What is bought of a tile on sale.
    enum class LandBuy : std::uint8_t
    {
        Land,   ///< The land itself, sold outright
        Rights, ///< Only the right to build on it
    };

    struct LandBought
    {
        std::int64_t spent = 0;     ///< Hundredths
        std::size_t tiles = 0;      ///< Bought
        bool short_of_cash = false; ///< A tile on sale was left for want of money
    };

    /// Buys every tile of `tiles` inside `grid` that is on sale as `what`, at `price` hundredths each, while
    /// `cash` lasts (none: money never runs out).
    LandBought buy_land(save::TerrainGrid& grid, std::span<const save::Tile> tiles, LandBuy what, std::int32_t price, std::optional<std::int64_t> cash);

} // namespace rct3::park
