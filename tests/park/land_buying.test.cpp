// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/land_buying.hpp"

#include <array>
#include <doctest/doctest.h>
#include <initializer_list>
#include <utility>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid strip(std::initializer_list<std::uint8_t> flags)
    {
        save::TerrainGrid g;
        g.width = static_cast<int>(flags.size());
        g.height = 1;
        for (const std::uint8_t f : flags)
            g.cells.push_back({ .flags = f });
        return g;
    }
    constexpr auto kForSale = std::to_underlying(save::TileFlag::ForSale);
    constexpr auto kRights = std::to_underlying(save::TileFlag::RightsOnly);
    constexpr auto kOwned = std::to_underlying(save::TileFlag::Owned);
} // namespace

TEST_CASE("buying land takes only the tiles on sale as that kind, each at its price")
{
    save::TerrainGrid g = strip({ kForSale, kForSale | kRights, kOwned, 0, kForSale });
    const std::array<save::Tile, 6> all { save::Tile { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 9, 0 } };
    const LandBought land = buy_land(g, all, LandBuy::Land, 300, std::nullopt);
    CHECK(land.tiles == 2);
    CHECK(land.spent == 600);
    CHECK(g.at(0, 0).ownership() == save::Ownership::Owned);
    CHECK(g.at(4, 0).ownership() == save::Ownership::Owned);
    CHECK(g.at(1, 0).ownership() == save::Ownership::ForSaleRights);
    CHECK(g.at(3, 0).ownership() == save::Ownership::Unowned);

    const LandBought rights = buy_land(g, all, LandBuy::Rights, 100, std::nullopt);
    CHECK(rights.tiles == 1);
    CHECK(g.at(1, 0).ownership() == save::Ownership::OwnedRights);
}

TEST_CASE("buying land stops at the tiles the park cannot pay for, and says so")
{
    save::TerrainGrid g = strip({ kForSale, kForSale, kForSale });
    const std::array<save::Tile, 3> all { save::Tile { 0, 0 }, { 1, 0 }, { 2, 0 } };
    const LandBought bought = buy_land(g, all, LandBuy::Land, 300, 700);
    CHECK(bought.tiles == 2);
    CHECK(bought.spent == 600);
    CHECK(bought.short_of_cash);
    CHECK(g.at(2, 0).ownership() == save::Ownership::ForSale);
}
