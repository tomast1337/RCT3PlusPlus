// SPDX-License-Identifier: GPL-3.0-or-later
//
// TrueType fonts rendered ahead of time into glyph pages, for text the game's own fonts cannot draw. Only
// the code ranges asked for are rendered, at one size; the result has the same shape as a game font (glyph
// boxes on the baseline, page coordinates, advances) so text layout does not tell them apart.
//
// The renderer trusts the file's tables: read fonts we ship or fetch with a checksum, never files from saves
// or user content.
#pragma once

#include "ovl/texture_image.hpp"
#include "ui/font.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// A TrueType file read into memory and checked to be one.
    struct TrueTypeFile
    {
        std::vector<std::uint8_t> data;
        float ascent_per_em = 0;  ///< Baseline to the top of a line, as a fraction of the em
        float descent_per_em = 0; ///< Baseline to the bottom of a line, as a fraction of the em
        float cap_per_em = 0;     ///< Height of a capital H, as a fraction of the em; the ascent without one
    };

    [[nodiscard]] std::expected<TrueTypeFile, std::string> read_truetype(std::vector<std::uint8_t> data);
    [[nodiscard]] std::expected<TrueTypeFile, std::string> load_truetype(const std::filesystem::path& path);

    /// Inclusive range of UTF-16 code units.
    struct CodeRange
    {
        char16_t first = 0;
        char16_t last = 0;
    };

    struct BakedGlyph
    {
        char16_t code = 0;
        Rect box; ///< Relative to the pen on the baseline, y down, whole pixels
        Rect uv;  ///< In its page, top row first
        std::uint32_t page = 0;
        float advance = 0; ///< Whole pixels
    };

    struct BakedFont
    {
        float line_height = 0;
        float descent = 0;
        std::vector<BakedGlyph> glyphs;
        std::vector<ovl::RgbaImage> pages; ///< White, coverage in alpha, rows from the top
    };

    struct BakeOptions
    {
        std::uint32_t page_side = 1024; ///< Pages are square
        /// Pixels are either covered or not (half coverage or more), with no smoothing. For pixel fonts drawn
        /// at sizes their grid does not divide: blocky but sharp instead of blurred.
        bool hard_edges = false;
    };

    /// Renders the codes in `ranges` the font has, with the em `em_pixels` tall. Codes the font lacks are
    /// left out.
    [[nodiscard]] std::expected<BakedFont, std::string> bake_truetype(const TrueTypeFile& file, float em_pixels, std::span<const CodeRange> ranges, const BakeOptions& options = { });

    /// The baked font on its uploaded pages, in the order of BakedFont::pages.
    [[nodiscard]] Font font_from_baked(const BakedFont& baked, std::span<const Font::Page> pages);

} // namespace rct3::ui
