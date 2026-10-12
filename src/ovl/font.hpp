// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fonts: a face description plus a glyph table that maps character codes to rectangles of a texture.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Bytes of one glyph row in the file: nine 32-bit words.
inline constexpr std::size_t kFontGlyphSize = 36;

struct FontGlyph {
  std::uint32_t code = 0;  ///< Character code, a UTF-16 code unit
  /// Horizontal step to the next glyph, as a fraction of the texture cell size. Appears to be the advance.
  float advance = 0;
  /// Second float of the row. Small, never above the advance in files seen, so probably a left offset.
  float lead = 0;
  /// Rectangle of the glyph in the font texture, as {u0, v0, u1, v1}. All zero for unused rows.
  std::array<float, 4> texcoords{};
  /// Last two floats of the row: whole numbers from -2 up to 8. Purpose unknown.
  std::array<float, 2> shift{};
};

struct Font {
  std::string_view face;     ///< Name of the typeface the glyphs were made from, a string in the file
  std::string_view texture;  ///< Name of the texture item holding the glyphs, without the tag. Empty if none.
  /// Three fractions of the font's cell size. Purpose unknown, the second is close to 1.
  std::array<float, 3> metrics{};
  std::uint32_t weight = 0;      ///< 400 normal or 700 bold
  /// Code of the first glyph for fonts without a code table (glyph i has code first_char + i), 0 otherwise.
  std::uint32_t first_char = 0;
  std::uint32_t flag = 0;        ///< 0 or 1, purpose unknown
  std::vector<FontGlyph> glyphs;
};

struct FontItem {
  std::string_view name;       ///< Symbol name without the tag
  std::optional<Font> font;    ///< Nothing when any part of the structure is unreadable
};

/// Fonts of one part of an overlay: pointer symbols tagged "fct". Views in the result point into the
/// overlay's bytes.
[[nodiscard]] std::vector<FontItem> read_fonts(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
