// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/gui_skin.hpp"

using namespace rct3::test;
using rct3::ovl::GuiSkinKind;
using rct3::ovl::Overlay;
using rct3::ovl::read_gui_skins;

namespace {

// Name block, 12 bytes: "S:gsi" 0, "Img:tex" 6 (list ends with NUL, padded). Structure block at address 16.
constexpr std::uint32_t kBase = 16;
constexpr std::uint32_t kNameTexture = 6;

// Offsets inside the structure block: item (16 bytes), then eight coordinates.
constexpr std::uint32_t kItem = 0;
constexpr std::uint32_t kCoordinates = 16;
constexpr std::uint32_t kEnd = 48;

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

std::vector<std::byte> names() {
  std::string text("S:gsi\0Img:tex\0", 14);
  text.resize(kBase, '\0');
  return bytes_of_string(text);
}

std::vector<std::byte> symbol_row() {
  std::vector<std::byte> out;
  words(out, {0, kBase + kItem});
  append_u16(out, 1);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::vector<std::byte> reference_row(std::uint32_t name = kNameTexture) {
  std::vector<std::byte> out;
  words(out, {kBase + kItem + 4, name, 0, 0});
  return out;
}

std::vector<std::byte> structure(std::uint32_t kind, std::uint32_t coordinates = kBase + kCoordinates) {
  std::vector<std::byte> out;
  words(out, {kind, 0, coordinates, 0});
  words(out, {10, 20, 30, 40});
  words(out, {static_cast<std::uint32_t>(-5), 60, 70, 80});
  REQUIRE(out.size() == kEnd);
  return out;
}

Overlay make(const std::vector<std::byte>& block, bool with_reference = true) {
  std::vector<std::byte> refs;
  if (with_reference) refs = reference_row();
  auto overlay =
      Overlay::from_bytes(build_file(5, {{names()}, {block}, {symbol_row(), {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("gui skins expose kind, texture and coordinates") {
  SUBCASE("a rectangle has four coordinates") {
    const auto overlay = make(structure(static_cast<std::uint32_t>(GuiSkinKind::Rect)));
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "S");
    REQUIRE(items[0].skin.has_value());
    const auto& s = *items[0].skin;
    CHECK(s.kind == 0);
    CHECK(s.texture == "Img");
    REQUIRE(s.coordinates.size() == 4);
    CHECK(s.coordinates[0] == 10);
    CHECK(s.coordinates[3] == 40);
  }

  SUBCASE("a nine patch has eight coordinates, negatives kept") {
    const auto overlay = make(structure(static_cast<std::uint32_t>(GuiSkinKind::NinePatch)));
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items[0].skin.has_value());
    REQUIRE(items[0].skin->coordinates.size() == 8);
    CHECK(items[0].skin->coordinates[4] == -5);
  }

  SUBCASE("three patches have six coordinates") {
    for (auto kind : {GuiSkinKind::HorizontalThree, GuiSkinKind::VerticalThree}) {
      const auto overlay = make(structure(static_cast<std::uint32_t>(kind)));
      const auto items = read_gui_skins(overlay, overlay.common());
      REQUIRE(items[0].skin.has_value());
      CHECK(items[0].skin->coordinates.size() == 6);
    }
  }

  SUBCASE("a solid item has none and needs no pointer") {
    const auto overlay = make(structure(static_cast<std::uint32_t>(GuiSkinKind::Solid), 0));
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items[0].skin.has_value());
    CHECK(items[0].skin->coordinates.empty());
  }
}

TEST_CASE("gui skin edge cases") {
  SUBCASE("an unknown kind is unreadable") {
    const auto overlay = make(structure(9));
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].skin.has_value());
  }

  SUBCASE("coordinates that leave the block make the item unreadable") {
    const auto overlay = make(structure(static_cast<std::uint32_t>(GuiSkinKind::NinePatch), kBase + kEnd - 8));
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].skin.has_value());
  }

  SUBCASE("a missing reference leaves the texture empty") {
    const auto overlay = make(structure(static_cast<std::uint32_t>(GuiSkinKind::Rect)), false);
    const auto items = read_gui_skins(overlay, overlay.common());
    REQUIRE(items[0].skin.has_value());
    CHECK(items[0].skin->texture.empty());
  }

  SUBCASE("a reference naming another type is not used") {
    // The only name of another type in the block is the item's own symbol name, "S:gsi".
    auto overlay = Overlay::from_bytes(
        build_file(5, {{names()}, {structure(0)}, {symbol_row(), {}, reference_row(0)}}, {}), std::nullopt);
    REQUIRE(overlay.has_value());
    const auto items = read_gui_skins(*overlay, overlay->common());
    REQUIRE(items[0].skin.has_value());
    CHECK(items[0].skin->texture.empty());
  }
}
