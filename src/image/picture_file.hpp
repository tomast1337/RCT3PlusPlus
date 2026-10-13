// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pictures stored as ordinary image files: JPEG, PNG, BMP and TGA, as photos and screenshots are. They decode to
// the same RGBA images the game's textures do.
#pragma once

#include "ovl/texture_image.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rct3::image
{

    /// The picture in `data`, opaque unless the file has alpha. An error for anything else, or for a side
    /// above ovl::kMaxImageSide.
    [[nodiscard]] std::expected<ovl::RgbaImage, std::string> decode_picture(std::span<const std::byte> data);

    /// Reads and decodes the file at `path`.
    [[nodiscard]] std::expected<ovl::RgbaImage, std::string> load_picture(const std::filesystem::path& path);

    /// Whether `path` names a kind of picture decode_picture() reads, by its extension in any case.
    [[nodiscard]] bool is_picture_file(const std::filesystem::path& path);

    /// The picture files directly in `folder`, by name. Empty when the folder is not there or cannot be read.
    [[nodiscard]] std::vector<std::filesystem::path> list_pictures(const std::filesystem::path& folder);

} // namespace rct3::image
