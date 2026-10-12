// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include "ovl/tracked_ride.hpp"
#include "record_file.hpp"

using namespace rct3::test;
using rct3::ovl::read_tracked_rides;

TEST_CASE("version 1 tracked rides start with the attraction fields") {
  Pool pool;
  pool.add("Coaster:trr");
  pool.add("CoasterName:txt");
  pool.add("CoasterDescription:txt");
  pool.add("CoasterIcon:gsi");
  pool.add("CoasterSpline:spl");
  std::vector<std::byte> bytes;
  put(bytes, {3, 0, 0, 0, 10000, static_cast<std::uint32_t>(-7500), 0, 0, 2500, 0});
  bytes.resize(0x28 + 8, std::byte{0});
  const auto overlay = make_record_overlay(1, pool, "Coaster:trr", bytes,
                                           {{4, "CoasterName:txt"}, {8, "CoasterDescription:txt"},
                                            {0xC, "CoasterIcon:gsi"}, {0x24, "CoasterSpline:spl"}});
  const auto items = read_tracked_rides(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].ride.has_value());
  const auto& i = items[0].ride->info;
  CHECK(i.type_id == 3);
  CHECK(i.name_text == "CoasterName");
  CHECK(i.description_text == "CoasterDescription");
  CHECK(i.icon == "CoasterIcon");
  CHECK(i.spline == "CoasterSpline");
  CHECK(i.value_a == 10000);
  CHECK(i.value_b == -7500);
  CHECK(i.value_c == 2500);
}

TEST_CASE("version 5 tracked rides give a name, track sections and splines") {
  Pool pool;
  const auto name = pool.add("Coaster");
  pool.add("Coaster:trr");
  pool.add("Lift:tks");
  pool.add("Path:spl");
  std::vector<std::byte> bytes;
  put(bytes, {0xFFFFFFFF, name});
  bytes.resize(0x124 + 8, std::byte{0});
  const auto overlay = make_record_overlay(5, pool, "Coaster:trr", bytes,
                                           {{0xD8, "Lift:tks"}, {0x114, "Path:spl"}, {0x120, "Path:spl"}});
  const auto items = read_tracked_rides(overlay, overlay.common());
  REQUIRE(items[0].ride.has_value());
  const auto& r = *items[0].ride;
  CHECK(r.name == "Coaster");
  CHECK(r.track_sections[0].empty());
  CHECK(r.track_sections[1] == "Lift");
  CHECK(r.splines[0] == "Path");
  CHECK(r.splines[3] == "Path");
  CHECK(r.info.name_text.empty());
}
