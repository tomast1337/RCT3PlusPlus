// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include "ovl/ride_car.hpp"
#include "record_file.hpp"

using namespace rct3::test;
using rct3::ovl::read_ride_cars;

TEST_CASE("ride cars expose their names, kind and visuals") {
  constexpr std::uint32_t sizes[] = {0, 240, 0, 0, 292, 336};
  for (std::uint32_t version : {1u, 4u, 5u}) {
    CAPTURE(version);
    Pool pool;
    const auto name = pool.add("TestCar");
    const auto display = pool.add("Test Car");
    pool.add("Test:ric");
    pool.add("testcar:svd");
    pool.add("wheel:svd");
    std::vector<std::byte> bytes;
    put(bytes, {name, display, 11, 0});
    bytes.resize(sizes[version], std::byte{0});
    const auto overlay = make_record_overlay(version, pool, "Test:ric", bytes, {{0x0C, "testcar:svd"}, {0x14, "wheel:svd"}});
    const auto items = read_ride_cars(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Test");
    REQUIRE(items[0].car.has_value());
    const auto& car = *items[0].car;
    CHECK(car.name == "TestCar");
    CHECK(car.display_name == "Test Car");
    CHECK(car.kind == 11);
    CHECK(car.visual == "testcar");
    REQUIRE(car.references.size() == 2);
    CHECK(car.references[1].name == "wheel");
    CHECK(car.record.size() == sizes[version]);
  }
}

TEST_CASE("a ride car at the end of its block is unreadable") {
  Pool pool;
  pool.add("Test:ric");
  std::vector<std::byte> bytes(200, std::byte{0});
  const auto overlay = make_record_overlay(1, pool, "Test:ric", bytes);
  const auto items = read_ride_cars(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK_FALSE(items[0].car.has_value());
}
