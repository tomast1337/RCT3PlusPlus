// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/float_value.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_floats;

namespace {

// Names: "A:flt" 0, "B:flt" 6, "C:flt" 12, "D:int" 18, "E:flt" 24. 30 bytes with the final NUL.
const std::string kNames("A:flt\0B:flt\0C:flt\0D:int\0E:flt\0", 30);

std::vector<std::byte> row(std::uint32_t name, std::uint32_t data, bool pointer) {
  std::vector<std::byte> out;
  append_u32(out, name);
  append_u32(out, data);
  append_u16(out, pointer ? 1 : 0);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

Overlay make(const std::vector<std::vector<std::byte>>& rows) {
  std::vector<std::byte> table;
  for (const auto& r : rows) table.insert(table.end(), r.begin(), r.end());
  auto overlay = Overlay::from_bytes(build_file(5, {{bytes_of_string(kNames)}, {}, {table}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("float values are read from the symbol data field") {
  const auto overlay = make({row(0, bits(12.5f), false), row(6, bits(-0.25f), false), row(12, 0, false)});
  const auto items = read_floats(overlay, overlay.common());
  REQUIRE(items.size() == 3);
  CHECK(items[0].name == "A");
  CHECK(items[0].value == 12.5f);
  CHECK(items[1].name == "B");
  CHECK(items[1].value == -0.25f);
  CHECK(items[2].name == "C");
  CHECK(items[2].value == 0.0f);
}

TEST_CASE("float value filtering") {
  SUBCASE("other tags are skipped") {
    const auto overlay = make({row(18, bits(1.0f), false), row(24, bits(2.0f), false)});
    const auto items = read_floats(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "E");
  }

  SUBCASE("a pointer symbol with the tag is not a value") {
    const auto overlay = make({row(0, 0x40, true), row(6, bits(3.0f), false)});
    const auto items = read_floats(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "B");
  }

  SUBCASE("a part without symbols has no values") {
    auto overlay = Overlay::from_bytes(build_file(5, {{bytes_of_string(kNames)}}, {}), std::nullopt);
    REQUIRE(overlay.has_value());
    CHECK(read_floats(*overlay, overlay->common()).empty());
  }

  SUBCASE("a non-finite pattern is kept as read") {
    const auto overlay = make({row(0, 0x7FC00000u, false)});
    const auto items = read_floats(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].value != items[0].value);
  }
}
