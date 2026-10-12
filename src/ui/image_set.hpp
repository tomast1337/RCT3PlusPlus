// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every texture of an overlay that has pixels, by name, turned the right way up (GUI images are stored
// bottom row first). For overlays whose images are used whole, such as logos, rather than cut into skins.
#pragma once

#include "ovl/texture_image.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <string>

namespace rct3::ui
{

    using ImageSet = std::map<std::string, ovl::RgbaImage, std::less<>>;

    /// Reads the overlay at `path` (either file of the pair, or the stem).
    [[nodiscard]] std::expected<ImageSet, std::string> load_image_set(const std::filesystem::path& path);

} // namespace rct3::ui
