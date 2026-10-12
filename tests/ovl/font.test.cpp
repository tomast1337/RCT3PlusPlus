// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/font.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_fonts;

namespace {

// Name block: "F:fct" 0, "Tx:tex" 6, padded to 16. The structure block follows at address 16.
constexpr std::uint32_t kBase = 16;
constexpr std::uint32_t kNameTexture = 6;

// Offsets inside the structure block.
constexpr std::uint32_t kItem = 0;    // 112 bytes
constexpr std::uint32_t kGlyphs = 112;  // 3 * 36
constexpr std::uint32_t kCodes = 220;   // 3 * 2, padded to 8
constexpr std::uint32_t kEnd = 228;

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

void real(std::vector<std::byte>& out, float v) { append_u32(out, std::bit_cast<std::uint32_t>(v)); }

std::vector<std::byte> names() {
  std::string text("F:fct\0Tx:tex\0", 13);
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

std::vector<std::byte> reference_row(std::uint32_t location = kBase + kItem + 20) {
  std::vector<std::byte> out;
  words(out, {location, kNameTexture, 0, 0});
  return out;
}

// `glyph_count` is how many glyph rows the item claims, the table itself always has 3.
std::vector<std::byte> structure(std::uint32_t glyph_count = 3, std::uint32_t code_address = kBase + kCodes,
                                 std::uint32_t glyph_address = kBase + kGlyphs, std::uint32_t first_char = 0) {
  std::vector<std::byte> out;
  for (float f : {0.25f, 0.75f, 0.125f}) real(out, f);
  words(out, {first_char, first_char + glyph_count, 0, 0x10001, 1, 0, 700});
  std::string face("Face");
  face.resize(64, '\xCC');
  face[4] = '\0';
  const auto raw = bytes_of_string(face);
  out.insert(out.end(), raw.begin(), raw.end());
  words(out, {glyph_address, code_address});
  for (std::uint32_t g = 0; g < 3; ++g) {
    real(out, 0.5f + 0.25f * g);  // advance
    real(out, 0.0625f);           // lead
    words(out, {0xDEADBEEF});     // leftover bytes
    for (float f : {0.1f * g, 0.2f, 0.1f * g + 0.05f, 0.3f}) real(out, f);
    real(out, 1.0f);
    real(out, -2.0f);
  }
  append_u16(out, 65);
  append_u16(out, 66);
  append_u16(out, 0x263A);
  append_u16(out, 0);
  REQUIRE(out.size() == kEnd);
  return out;
}

Overlay make(const std::vector<std::byte>& block, const std::vector<std::byte>& refs = reference_row()) {
  auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {block}, {symbol_row(), {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("fonts expose face, texture and glyphs") {
  const auto overlay = make(structure());
  const auto items = read_fonts(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK(items[0].name == "F");
  REQUIRE(items[0].font.has_value());

  const auto& f = *items[0].font;
  CHECK(f.face == "Face");
  CHECK(f.texture == "Tx");
  CHECK(f.metrics[0] == 0.25f);
  CHECK(f.metrics[1] == 0.75f);
  CHECK(f.metrics[2] == 0.125f);
  CHECK(f.weight == 700);
  CHECK(f.first_char == 0);
  CHECK(f.flag == 1);

  REQUIRE(f.glyphs.size() == 3);
  CHECK(f.glyphs[0].code == 65);
  CHECK(f.glyphs[1].code == 66);
  CHECK(f.glyphs[2].code == 0x263A);
  CHECK(f.glyphs[1].advance == 0.75f);
  CHECK(f.glyphs[1].lead == 0.0625f);
  CHECK(f.glyphs[0].texcoords[1] == 0.2f);
  CHECK(f.glyphs[0].texcoords[3] == 0.3f);
  CHECK(f.glyphs[2].shift[0] == 1.0f);
  CHECK(f.glyphs[2].shift[1] == -2.0f);
}

TEST_CASE("font edge cases") {
  SUBCASE("without a code table, codes count up from the first character") {
    const auto overlay = make(structure(3, 0, kBase + kGlyphs, 45));
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].font.has_value());
    REQUIRE(items[0].font->glyphs.size() == 3);
    CHECK(items[0].font->glyphs[0].code == 45);
    CHECK(items[0].font->glyphs[2].code == 47);
  }

  SUBCASE("an end character below the first character is refused") {
    auto block = structure(3, 0, kBase + kGlyphs, 45);
    block[16] = std::byte{10};  // end = 10 < first = 45
    block[17] = block[18] = block[19] = std::byte{0};
    const auto overlay = make(block);
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].font.has_value());
  }

  SUBCASE("a glyph count that cannot fit is refused") {
    const auto overlay = make(structure(0x7FFFFFF0));
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].font.has_value());
  }

  SUBCASE("a glyph table that leaves its block makes the font unreadable") {
    const auto overlay = make(structure(3, kBase + kCodes, kBase + kEnd - 8));
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].font.has_value());
  }

  SUBCASE("a code table that leaves its block makes the font unreadable") {
    const auto overlay = make(structure(3, kBase + kEnd - 2));
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].font.has_value());
  }

  SUBCASE("a face name without a terminator is unreadable") {
    auto block = structure();
    for (std::size_t i = 40; i < 104; ++i) block[i] = std::byte{'x'};
    const auto overlay = make(block);
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].font.has_value());
  }

  SUBCASE("a missing texture reference leaves the name empty") {
    const auto overlay = make(structure(), reference_row(kBase + 8));
    const auto items = read_fonts(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].font.has_value());
    CHECK(items[0].font->texture.empty());
  }
}
