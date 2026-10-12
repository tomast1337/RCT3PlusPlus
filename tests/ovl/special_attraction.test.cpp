// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include "ovl/special_attraction.hpp"
#include "record_file.hpp"

using namespace rct3::test;
using rct3::ovl::read_special_attractions;

TEST_CASE("version 1 special attractions are 13 words") {
  Pool pool;
  pool.add("Toilet:sat");
  pool.add("ToiletName:txt");
  pool.add("ToiletDescription:txt");
  pool.add("ToiletIcon:gsi");
  pool.add("Toilet01:sid");
  pool.add("ToiletSpline:spl");
  std::vector<std::byte> bytes;
  put(bytes, {12, 0, 0, 0, 10000, static_cast<std::uint32_t>(-7500), 0, 0, 2500, 0, 0, 0, 0});
  const auto overlay = make_record_overlay(
      1, pool, "Toilet:sat", bytes,
      {{4, "ToiletName:txt"}, {8, "ToiletDescription:txt"}, {0xC, "ToiletIcon:gsi"},
       {0x24, "ToiletSpline:spl"}, {0x30, "Toilet01:sid"}});
  const auto items = read_special_attractions(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].attraction.has_value());
  const auto& a = *items[0].attraction;
  CHECK(a.info.type_id == 12);
  CHECK(a.info.style == "Toilet01");
  CHECK(a.info.spline == "ToiletSpline");
  CHECK(a.info.value_c == 2500);
  CHECK(a.name.empty());
}

TEST_CASE("version 5 special attractions carry a name string") {
  Pool pool;
  const auto name = pool.add("Cash Machine");
  pool.add("CashMachine:sat");
  pool.add("CashMachine:sid");
  pool.add("CashMachineName:txt");
  std::vector<std::byte> bytes;
  put(bytes, {2, name, 0, 768 + 12});
  bytes.resize(80, std::byte{0});
  const auto overlay = make_record_overlay(5, pool, "CashMachine:sat", bytes,
                                           {{8, "CashMachine:sid"}, {0x10, "CashMachineName:txt"}});
  const auto items = read_special_attractions(overlay, overlay.common());
  REQUIRE(items[0].attraction.has_value());
  const auto& a = *items[0].attraction;
  CHECK(a.name == "Cash Machine");
  CHECK(a.info.type_id == 12);
  CHECK(a.info.style == "CashMachine");
  CHECK(a.info.name_text == "CashMachineName");
}

TEST_CASE("version 4 special attractions are not interpreted") {
  Pool pool;
  pool.add("Toilet:sat");
  std::vector<std::byte> bytes(80, std::byte{0});
  const auto overlay = make_record_overlay(4, pool, "Toilet:sat", bytes);
  const auto items = read_special_attractions(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK_FALSE(items[0].attraction.has_value());
}
