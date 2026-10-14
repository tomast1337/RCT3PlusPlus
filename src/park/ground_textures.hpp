// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pictures the ground is painted with. The game's terrain files list terrain types, each naming a
// texture and carrying its index: land surfaces and cliff faces are numbered separately, and only the
// type's own name tells them apart (cliff faces' start with "Cliff_"). The pictures are kept by a key made
// from what they paint and their index, which is how the terrain mesh's batches name them.
#pragma once

#include "ovl/texture_image.hpp"
#include "park/terrain_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace rct3::park
{

    using GroundImages = std::map<std::string, ovl::RgbaImage, std::less<>>;

    [[nodiscard]] GroundPaint ground_paint(std::string_view terrain_type);
    [[nodiscard]] std::string ground_key(GroundPaint paint, std::uint8_t index);

    /// Adds the pictures of every terrain type in the overlay at `path` to `images`, replacing any with the
    /// same key. Returns how many types had no usable picture.
    [[nodiscard]] std::expected<std::size_t, std::string> add_ground_images(GroundImages& images, const std::filesystem::path& path);

} // namespace rct3::park
