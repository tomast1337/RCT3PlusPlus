// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writing an RGBA image as a PNG file: screenshots, and developer tools that save what they drew. The pixels
// are stored without compression, so the files are large but the encoder stays a page long; any viewer reads
// them.
// shortcut: no compression (about 4 bytes a pixel), add deflate when screenshots and photos pile up.
#pragma once

#include "ovl/texture_image.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::image
{

    /// The PNG file of `image`: 8 bits per channel with alpha. Empty when the image has no pixels or fewer
    /// bytes than its size needs.
    [[nodiscard]] std::vector<std::byte> encode_png(const ovl::RgbaImage& image);

    /// Writes encode_png(image) to `path`.
    [[nodiscard]] std::expected<void, std::string> save_png(const std::filesystem::path& path, const ovl::RgbaImage& image);

    /// Writes `image` into `folder` (which must exist) as `<prefix>NNNN.png`, numbered one past the highest
    /// number of the folder's files of that prefix, so it never replaces a file and numbers keep rising after
    /// some are deleted. Returns the path written.
    [[nodiscard]] std::expected<std::filesystem::path, std::string> save_numbered_png(const std::filesystem::path& folder, std::string_view prefix, const ovl::RgbaImage& image);

} // namespace rct3::image
