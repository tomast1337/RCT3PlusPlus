// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include "ovl/animated_ride.hpp"
#include "record_file.hpp"

using namespace rct3::test;
using rct3::ovl::read_animated_rides;

TEST_CASE("version 5 animated rides, with the type id offset removed") {
  Pool pool;
  pool.add("Carousel:anr");
  pool.add("CarouselName:txt");
  pool.add("CarouselDescription:txt");
  pool.add("CarouselIcon:gsi");
  pool.add("Carousel:sid");
  pool.add("Path:spl");
  std::vector<std::uint32_t> w(0xC4 / 4 + 2, 0);
  w[0x70 / 4] = 768 + 4;
  w[0x80 / 4] = 626;
  w[0x84 / 4] = static_cast<std::uint32_t>(-566);
  w[0x90 / 4] = 4960;
  std::vector<std::byte> bytes;
  for (auto v : w) append_u32(bytes, v);
  const auto overlay = make_record_overlay(
      5, pool, "Carousel:anr", bytes,
      {{0x18, "Carousel:sid"}, {0x74, "CarouselName:txt"}, {0x78, "CarouselDescription:txt"},
       {0x7C, "CarouselIcon:gsi"}, {0x94, "Path:spl"}, {0xB4, "Path:spl"}, {0xC0, "Path:spl"}});
  const auto items = read_animated_rides(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].ride.has_value());
  const auto& r = *items[0].ride;
  CHECK(r.info.type_id == 4);
  CHECK(r.info.name_text == "CarouselName");
  CHECK(r.info.description_text == "CarouselDescription");
  CHECK(r.info.icon == "CarouselIcon");
  CHECK(r.info.style == "Carousel");
  CHECK(r.info.spline == "Path");
  CHECK(r.info.value_a == 626);
  CHECK(r.info.value_b == -566);
  CHECK(r.info.value_c == 4960);
  CHECK(r.splines[0] == "Path");
  CHECK(r.splines[3] == "Path");
}

TEST_CASE("a version 5 animated ride without its last spline slot is still read") {
  Pool pool;
  pool.add("Carousel:anr");
  std::vector<std::byte> bytes(0xC0, std::byte{0});  // ends where the fourth slot would start
  const auto overlay = make_record_overlay(5, pool, "Carousel:anr", bytes);
  const auto items = read_animated_rides(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].ride.has_value());
  CHECK(items[0].ride->splines[3].empty());
}

TEST_CASE("an animated ride too short for its fields is unreadable") {
  Pool pool;
  pool.add("Carousel:anr");
  std::vector<std::byte> bytes(0x40, std::byte{0});
  const auto overlay = make_record_overlay(5, pool, "Carousel:anr", bytes);
  const auto items = read_animated_rides(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK_FALSE(items[0].ride.has_value());
}
