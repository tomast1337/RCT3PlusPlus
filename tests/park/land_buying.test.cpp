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
    save::model::TerrainGrid strip(std::initializer_list<std::uint8_t> flags)
    {
        save::model::TerrainGrid g;
        g.width = static_cast<int>(flags.size());
        g.height = 1;
        for (const std::uint8_t f : flags)
            g.cells.push_back({ .flags = f });
        return g;
    }
    constexpr auto kForSale = std::to_underlying(save::model::TileFlag::ForSale);
    constexpr auto kRights = std::to_underlying(save::model::TileFlag::RightsOnly);
    constexpr auto kOwned = std::to_underlying(save::model::TileFlag::Owned);
} // namespace

TEST_CASE("buying land takes only the tiles on sale as that kind, each at its price")
{
    save::model::TerrainGrid g = strip({ kForSale, kForSale | kRights, kOwned, 0, kForSale });
    const std::array<save::model::Tile, 6> all { save::model::Tile { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 9, 0 } };
    const LandBought land = buy_land(g, all, LandBuy::Land, units::Money { 300 }, std::nullopt);
    CHECK(land.tiles == 2);
    CHECK(land.spent == units::Money { 600 });
    CHECK(g.at(0, 0).ownership() == save::model::Ownership::Owned);
    CHECK(g.at(4, 0).ownership() == save::model::Ownership::Owned);
    CHECK(g.at(1, 0).ownership() == save::model::Ownership::ForSaleRights);
    CHECK(g.at(3, 0).ownership() == save::model::Ownership::Unowned);

    const LandBought rights = buy_land(g, all, LandBuy::Rights, units::Money { 100 }, std::nullopt);
    CHECK(rights.tiles == 1);
    CHECK(g.at(1, 0).ownership() == save::model::Ownership::OwnedRights);
}

TEST_CASE("buying land stops at the tiles the park cannot pay for, and says so")
{
    save::model::TerrainGrid g = strip({ kForSale, kForSale, kForSale });
    const std::array<save::model::Tile, 3> all { save::model::Tile { 0, 0 }, { 1, 0 }, { 2, 0 } };
    const LandBought bought = buy_land(g, all, LandBuy::Land, units::Money { 300 }, units::Money { 700 });
    CHECK(bought.tiles == 2);
    CHECK(bought.spent == units::Money { 600 });
    CHECK(bought.short_of_cash);
    CHECK(g.at(2, 0).ownership() == save::model::Ownership::ForSale);
}

TEST_CASE("land held back is not for sale until put on sale, then bought like any other")
{
    constexpr auto kHeldBack = std::to_underlying(save::model::TileFlag::HeldBack);
    save::model::TerrainGrid g = strip({ kHeldBack, kHeldBack | kForSale });
    const std::array<save::model::Tile, 2> all { save::model::Tile { 0, 0 }, { 1, 0 } };
    CHECK(g.at(0, 0).ownership() == save::model::Ownership::Locked);
    CHECK(g.at(1, 0).ownership() == save::model::Ownership::ForSale);
    const LandBought land = buy_land(g, all, LandBuy::Land, units::Money { 300 }, std::nullopt);
    CHECK(land.tiles == 1);
    CHECK(g.at(0, 0).ownership() == save::model::Ownership::Locked);
    CHECK(g.at(1, 0).ownership() == save::model::Ownership::Owned);
}
