// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ride_train.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_ride_trains;

TEST_CASE("ride trains list their cars")
{
    Pool pool;
    const auto name = pool.add("TestName");
    const auto display = pool.add("Test Train");
    pool.add("Test:rit");
    pool.add("Front:ric");
    pool.add("Back:ric");
    std::vector<std::byte> bytes;
    put(bytes, { name, display });
    bytes.resize(200, std::byte { 0 });
    const auto overlay = make_record_overlay(1, pool, "Test:rit", bytes, { { 0x08, "Front:ric" }, { 0x10, "Back:ric" } });
    const auto items = read_ride_trains(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].train.has_value());
    const auto& t = *items[0].train;
    CHECK(t.name == "TestName");
    CHECK(t.display_name == "Test Train");
    CHECK(t.cars[0] == "Front");
    CHECK(t.cars[1].empty());
    CHECK(t.cars[2] == "Back");
    CHECK(t.references.size() == 2);
    CHECK(t.record.size() == 184);
}

TEST_CASE("a name field of -1 gives an empty name")
{
    Pool pool;
    pool.add("Test:rit");
    std::vector<std::byte> bytes;
    put(bytes, { 0xFFFFFFFF });
    bytes.resize(200, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Test:rit", bytes);
    const auto items = read_ride_trains(overlay, overlay.common());
    REQUIRE(items[0].train.has_value());
    CHECK(items[0].train->name.empty());
}
