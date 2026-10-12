// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/integer_value.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_integers;

namespace {

// Names: "A:int" 0, "B:int" 6, "C:int" 12, "D:flt" 18, "E:int" 24. 30 bytes with the final NUL.
const std::string kNames("A:int\0B:int\0C:int\0D:flt\0E:int\0", 30);

std::vector<std::byte> row(std::uint32_t name, std::uint32_t data, bool pointer) {
  std::vector<std::byte> out;
  append_u32(out, name);
  append_u32(out, data);
  append_u16(out, pointer ? 1 : 0);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

Overlay make(const std::vector<std::vector<std::byte>>& rows) {
  std::vector<std::byte> table;
  for (const auto& r : rows) table.insert(table.end(), r.begin(), r.end());
  auto overlay = Overlay::from_bytes(build_file(5, {{bytes_of_string(kNames)}, {}, {table}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("integer values are read from the symbol data field") {
  const auto overlay = make({row(0, 7, false), row(6, 0, false), row(12, 0xFFFFFFFFu, false)});
  const auto items = read_integers(overlay, overlay.common());
  REQUIRE(items.size() == 3);
  CHECK(items[0].name == "A");
  CHECK(items[0].value == 7);
  CHECK(items[1].name == "B");
  CHECK(items[1].value == 0);
  CHECK(items[2].name == "C");
  CHECK(items[2].value == -1);
}

TEST_CASE("integer value filtering") {
  SUBCASE("other tags are skipped") {
    const auto overlay = make({row(18, 1, false), row(24, 2, false)});
    const auto items = read_integers(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "E");
    CHECK(items[0].value == 2);
  }

  SUBCASE("a pointer symbol with the tag is not a value") {
    const auto overlay = make({row(0, 0x40, true), row(6, 3, false)});
    const auto items = read_integers(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "B");
  }

  SUBCASE("a part without symbols has no values") {
    auto overlay = Overlay::from_bytes(build_file(5, {{bytes_of_string(kNames)}}, {}), std::nullopt);
    REQUIRE(overlay.has_value());
    CHECK(read_integers(*overlay, overlay->common()).empty());
  }
}
