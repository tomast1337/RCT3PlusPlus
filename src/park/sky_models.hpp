// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the sky's shapes and pictures from the game's sky overlay, with the background mountains. The
// shapes are found by name; each is drawn with the picture of a fixed name, which is not always the one the
// shape itself names. The mountains' two meshes take the two pictures of the park's landscape theme.
#pragma once

#include "ovl/texture_image.hpp"
#include "park/mountain_mesh.hpp"
#include "park/sky_mesh.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// The sky overlay, relative to the install.
    inline constexpr std::string_view kSkyFile = "Sky/Sky.common.ovl";

    struct LoadedSky
    {
        SkyModels models;
        MountainShapes mountains;                                  ///< In the pictures of the theme asked for
        std::map<std::string, ovl::RgbaImage, std::less<>> images; ///< By the shapes' texture keys
    };

    /// Makes a picture of light on black see-through by its darkness, so drawn over something it adds its
    /// light rather than covering it in black: each pixel keeps its hue at full brightness and becomes as
    /// opaque as it was bright.
    void glowing(ovl::RgbaImage& image);

    /// The sky in the overlay at `path`, with the background mountains of `theme` (see kBackgroundThemes). A
    /// shape or picture that is missing leaves that part of the sky out.
    [[nodiscard]] std::expected<LoadedSky, std::string> load_sky(const std::filesystem::path& path, std::string_view theme);

    /// The colours along the foot of the coloured dome, one per column of its picture from left to right, for
    /// whatever shows below it. Empty when the dome or its picture is missing.
    [[nodiscard]] std::vector<glm::vec4> horizon_band(const LoadedSky& sky);

} // namespace rct3::park
