// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the sky's shapes and pictures from the game's sky overlay. The shapes are found by name; each is
// drawn with the picture of a fixed name, which is not always the one the shape itself names.
#pragma once

#include "ovl/texture_image.hpp"
#include "park/sky_mesh.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::park
{

    /// The sky overlay, relative to the install.
    inline constexpr std::string_view kSkyFile = "Sky/Sky.common.ovl";

    struct LoadedSky
    {
        SkyModels models;
        std::map<std::string, ovl::RgbaImage, std::less<>> images; ///< By the shapes' texture keys
    };

    /// The sky in the overlay at `path`. A shape or picture that is missing leaves that part of the sky out.
    [[nodiscard]] std::expected<LoadedSky, std::string> load_sky(const std::filesystem::path& path);

    /// The colour at the foot of the coloured dome in column `day`, for whatever shows below it. Nothing
    /// when the dome or its picture is missing.
    [[nodiscard]] std::optional<glm::vec4> horizon_colour(const LoadedSky& sky, float day);

} // namespace rct3::park
