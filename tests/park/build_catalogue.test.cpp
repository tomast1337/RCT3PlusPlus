// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/build_catalogue.hpp"

#include "../ovl/record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;

TEST_CASE("rides go in the list of their attraction kind")
{
    CHECK(ride_list(0) == BuildList::TransportRides);
    CHECK(ride_list(21) == BuildList::TransportRides);
    CHECK(ride_list(1) == BuildList::GentleRides);
    CHECK(ride_list(2) == BuildList::WaterRides);
    for (const std::uint32_t coaster : { 3u, 15u, 16u, 17u })
        CHECK(ride_list(coaster) == BuildList::Coasters);
    CHECK(ride_list(4) == BuildList::ThrillRides);
    CHECK(ride_list(5) == BuildList::OtherRides);
    CHECK(ride_list(6) == BuildList::JuniorRides);
    CHECK(ride_list(19) == BuildList::PoolRides);
    for (const std::uint32_t none : { 7u, 10u, 14u, 18u, 20u, 22u })
        CHECK_FALSE(ride_list(none).has_value());
}

TEST_CASE("shops go in the list of their attraction kind")
{
    CHECK(shop_list(7) == BuildList::FoodShops);
    CHECK(shop_list(8) == BuildList::DrinkShops);
    CHECK(shop_list(9) == BuildList::Stalls);
    for (const std::uint32_t facility : { 11u, 12u, 13u, 14u })
        CHECK(shop_list(facility) == BuildList::Facilities);
    CHECK_FALSE(shop_list(10).has_value());
    CHECK_FALSE(shop_list(3).has_value());
}

TEST_CASE("scenery goes in the list of its category")
{
    CHECK(scenery_list(2) == BuildList::Trees);
    CHECK(scenery_list(4) == BuildList::Fences);
    CHECK(scenery_list(35) == BuildList::Walls);
    CHECK(scenery_list(31) == BuildList::PathExtras);
    CHECK(scenery_list(44) == BuildList::Props);
    CHECK(scenery_list(22) == BuildList::ParkEntrances);
    CHECK(scenery_list(28) == BuildList::RideEvents);
    CHECK(scenery_list(41) == BuildList::Fireworks);
    CHECK(scenery_list(43) == BuildList::GroundSurfaces);
    for (const std::uint32_t none : { 11u, 18u, 23u, 24u, 27u, 39u })
        CHECK_FALSE(scenery_list(none).has_value());
}

TEST_CASE("a stall is offered in its shop list")
{
    using namespace rct3::test;
    Pool pool;
    pool.add("Fizz:sta");
    pool.add("FizzName:txt");
    pool.add("FizzIcon:gsi");
    std::vector<std::byte> bytes;
    put(bytes, { 8, 0, 0, 0 });
    bytes.resize(0x30, std::byte { 0 });
    const auto overlay = make_record_overlay(1, pool, "Fizz:sta", bytes, { { 4, "FizzName:txt" }, { 0xC, "FizzIcon:gsi" } });
    std::vector<BuildItem> items;
    add_build_items(overlay, "Style/Made/Style", items);
    REQUIRE(items.size() == 1);
    CHECK(items[0].list == BuildList::DrinkShops);
    CHECK(items[0].kind == BuildKind::Shop);
    CHECK(items[0].symbol == "Fizz");
    CHECK(items[0].overlay == "Style/Made/Style");
    CHECK(items[0].name_text == "FizzName");
    CHECK(items[0].icon == "FizzIcon");
}

TEST_CASE("a park offers what it made available and invented")
{
    rct3::save::RequiredContent content;
    const auto entry = [](std::string stem, std::string symbol, bool available, bool invented, bool hidden) {
        rct3::save::AssetRequirement r;
        r.overlay_stem = std::move(stem);
        r.symbol = std::move(symbol);
        r.available = available;
        r.invented = invented;
        r.hidden = hidden;
        return r;
    };
    content.entries = { entry("Style/Made/style", "Fizz", true, true, false), entry("Style/Made/style", "Later", true, false, false), entry("Style/Made/style", "Gone", true, true, true) };
    const BuildOffer offer(content);
    CHECK(offer.known());
    const auto item = [](std::string symbol) {
        BuildItem i;
        i.overlay = "Style/Made/Style";
        i.symbol = std::move(symbol);
        return i;
    };
    CHECK(offer.offers(item("Fizz")));
    CHECK(offer.offers(item("fizz")));
    CHECK_FALSE(offer.offers(item("Later")));
    CHECK_FALSE(offer.offers(item("Gone")));
    CHECK_FALSE(offer.offers(item("Unlisted")));

    const BuildOffer open;
    CHECK_FALSE(open.known());
    CHECK(open.offers(item("Unlisted")));
}
