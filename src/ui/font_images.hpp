// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fonts of an overlay with their page images, ready to upload. Pages are kept with their rows as
// stored, which is what glyph texture coordinates count from (see Font for their direction).
#pragma once

#include "ovl/font.hpp"
#include "ovl/texture_image.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct FontImages
    {
        std::string name;
        /// The font item. Its `face` and `texture` views are cleared, since the overlay they pointed into
        /// is gone; `face` keeps the face's name.
        ovl::Font font;
        std::string face { };
        std::vector<ovl::RgbaImage> pages; ///< By page index; an unreadable page is an empty image
    };

    /// Reads every font of the overlay at `path` (either file of the pair, or the stem) whose texture has
    /// pixels in the same overlay.
    [[nodiscard]] std::expected<std::vector<FontImages>, std::string> load_font_images(const std::filesystem::path& path);

} // namespace rct3::ui
