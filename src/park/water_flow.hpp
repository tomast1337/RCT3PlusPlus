// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water running down slides and water rides. A track piece that carries water names two more splines,
// the water's left and right edges, in the same space as its rails. The water is a strip between them,
// sampled a few times along each part of the splines. Its picture runs once across the strip and repeats
// along it a whole number of times, so pieces laid end to end join without a seam; the scene makes it flow
// by rolling the picture's rows (flow_rows).
#pragma once

#include "ovl/spline.hpp"
#include "ovl/texture_image.hpp"
#include "park/object_models.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::park
{

    /// The overlay and texture of the flowing water: seen in the game's install.
    inline constexpr std::string_view kFlowFile = "WaterFlow/WaterFlow.common.ovl";
    inline constexpr std::string_view kFlowTexture = "WaterFlow";
    /// Key the strips' picture goes by among the models' pictures. It is a plain texture rather than one the
    /// models' pictures are painted from, so the scene loads it itself (load_flow_picture).
    inline constexpr std::string_view kFlowPicture = "rct3pp:flow";

    /// About how far the picture runs along the water before it repeats, in park units. Our own value.
    inline constexpr float kFlowRepeat = 4.0f;
    /// How far the water moves each second, in park units. Our own value.
    inline constexpr float kFlowSpeed = 3.0f;

    /// The water between `left` and `right`, textured `texture` and see-through. Nothing when the edges do
    /// not pair up node for node or have no length.
    [[nodiscard]] std::optional<ModelPart> flow_strip(const ovl::Spline& left, const ovl::Spline& right, std::string texture);

    /// How many rows a picture `rows` high is rolled once the water has flowed for `seconds`.
    [[nodiscard]] std::uint32_t flow_shift(std::uint32_t rows, float seconds);

    /// `image` with each row moved `shift` rows higher, wrapping round: the picture moves the way the strip's
    /// picture runs along the water.
    [[nodiscard]] ovl::RgbaImage flow_rows(const ovl::RgbaImage& image, std::uint32_t shift);

    /// The flowing water's picture from the overlay whose common file is `file`.
    [[nodiscard]] std::expected<ovl::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file);

} // namespace rct3::park
