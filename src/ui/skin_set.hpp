// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins of an overlay with the images they cut up, ready to upload. GUI images are stored with their
// bottom row first; the set turns them the right way up so skin coordinates count from the top row and
// rows go top to bottom, as the renderer expects.
#pragma once

#include "ovl/texture_image.hpp"
#include "ui/skin.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <string>

namespace rct3::ui
{

    struct NamedSkin
    {
        SkinLayout layout;
        std::string texture; ///< Key into SkinSet::images
    };

    struct SkinSet
    {
        std::map<std::string, NamedSkin, std::less<>> skins;
        /// Only images some skin uses.
        std::map<std::string, ovl::RgbaImage, std::less<>> images;
        std::size_t skipped = 0; ///< Skins whose layout or image could not be read
    };

    /// Reads every GUI skin of the overlay at `path` (either file of the pair, or the stem).
    [[nodiscard]] std::expected<SkinSet, std::string> load_skin_set(const std::filesystem::path& path);

} // namespace rct3::ui
