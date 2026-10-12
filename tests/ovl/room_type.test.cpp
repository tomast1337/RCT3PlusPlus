// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/room_type.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;

namespace {

constexpr char kNameData[] = "Cabin:chg\0Deck:vwg\0Piece:sid\0Path:spl\0Label:txt\0Icon:gsi";
const std::string kNames(kNameData, sizeof kNameData);
const std::uint32_t kBase = (static_cast<std::uint32_t>(sizeof kNameData) + 3) & ~3u;

std::uint32_t name_at(const std::string& needle) {
  const auto at = kNames.find(needle);
  REQUIRE(at != std::string::npos);
  return static_cast<std::uint32_t>(at);
}

std::vector<std::byte> names() {
  std::string text = kNames;
  text.resize(kBase, '\0');
  return bytes_of_string(text);
}

std::vector<std::byte> symbol(const std::string& full) {
  std::vector<std::byte> out;
  append_u32(out, name_at(full));
  append_u32(out, kBase);
  append_u16(out, 1);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::vector<std::byte> reference(std::uint32_t location, const std::string& name) {
  std::vector<std::byte> out;
  for (auto v : {location, name_at(name), std::uint32_t{0}, std::uint32_t{0}}) append_u32(out, v);
  return out;
}

std::vector<std::byte> words(std::size_t bytes, std::initializer_list<std::pair<std::size_t, std::uint32_t>> set) {
  std::vector<std::uint32_t> w(bytes / 4, 0);
  for (auto [at, v] : set) w.at(at / 4) = v;
  std::vector<std::byte> out;
  for (auto v : w) append_u32(out, v);
  return out;
}

Overlay make(const std::vector<std::byte>& data, const std::string& symbol_name,
             const std::vector<std::byte>& refs = {}) {
  auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {data}, {symbol(symbol_name), {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("changing rooms") {
  const auto data = words(76, {{0x0c, 111}, {0x2c, 222}, {0x40, 0xFFFFFFFFu}, {0x44, 1}, {0x20, 5}});
  std::vector<std::byte> refs;
  for (auto r : {reference(kBase + 4, "Piece:sid"), reference(kBase + 8, "Path:spl"),
                 reference(kBase + 0x10, "Label:txt"), reference(kBase + 0x14, "Label:txt")}) {
    refs.insert(refs.end(), r.begin(), r.end());
  }
  const auto overlay = make(data, "Cabin:chg", refs);
  const auto rooms = rct3::ovl::read_changing_rooms(overlay, overlay.common());
  REQUIRE(rooms.size() == 1);
  REQUIRE(rooms[0].room.has_value());
  const auto& r = *rooms[0].room;
  CHECK(rooms[0].name == "Cabin");
  CHECK(r.scenery_item == "Piece");
  CHECK(r.spline == "Path");
  CHECK(r.name_text == "Label");
  CHECK(r.description_text == "Label");
  CHECK(r.icon.empty());
  CHECK(r.unknown_0c == 111);
  CHECK(r.unknown_2c == 222);
  CHECK(r.unknown_40 == -1);
  CHECK(r.unknown_44 == 1);
  CHECK(r.reserved[1] == 5);
}

TEST_CASE("viewing galleries") {
  const auto data = words(72, {{0x04, 333}, {0x24, 444}, {0x38, 0xFFFFFFFFu}, {0x3c, 2}});
  std::vector<std::byte> refs;
  for (auto r : {reference(kBase + 8, "Label:txt"), reference(kBase + 0x10, "Icon:gsi")}) {
    refs.insert(refs.end(), r.begin(), r.end());
  }
  const auto overlay = make(data, "Deck:vwg", refs);
  const auto galleries = rct3::ovl::read_viewing_galleries(overlay, overlay.common());
  REQUIRE(galleries.size() == 1);
  REQUIRE(galleries[0].gallery.has_value());
  const auto& g = *galleries[0].gallery;
  CHECK(g.unknown_04 == 333);
  CHECK(g.name_text == "Label");
  CHECK(g.description_text.empty());
  CHECK(g.icon == "Icon");
  CHECK(g.unknown_24 == 444);
  CHECK(g.unknown_38 == -1);
  CHECK(g.unknown_3c == 2);
}

TEST_CASE("room structures cut short by their block are unreadable") {
  {
    const auto overlay = make(words(72, {}), "Cabin:chg");
    const auto rooms = rct3::ovl::read_changing_rooms(overlay, overlay.common());
    REQUIRE(rooms.size() == 1);
    CHECK_FALSE(rooms[0].room.has_value());
  }
  {
    const auto overlay = make(words(68, {}), "Deck:vwg");
    const auto galleries = rct3::ovl::read_viewing_galleries(overlay, overlay.common());
    REQUIRE(galleries.size() == 1);
    CHECK_FALSE(galleries[0].gallery.has_value());
  }
}

TEST_CASE("symbols of other tags are not room types") {
  const auto overlay = make(words(76, {}), "Cabin:chg");
  CHECK(rct3::ovl::read_viewing_galleries(overlay, overlay.common()).empty());
}
