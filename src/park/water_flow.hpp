// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water running down slides and water rides. A track piece that carries water names two more splines,
// the water's left and right edges, in the same space as its rails. The water is a strip between them,
// sampled a few times along each part of the splines. Its picture runs once across the strip and repeats
// along it a whole number of times, so pieces laid end to end join without a seam. Its picture runs the way
// the piece's track runs, whichever end the file lists its water from, and slides that way as the water flows;
// pieces laid against their section's own way slide it the other way round (flow_scroll).
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

    /// About how far the picture runs along the water before it repeats, in park units.
    inline constexpr float kFlowRepeat = 4.0f;
    /// How far the water moves each second, in park units.
    inline constexpr float kFlowSpeed = 3.0f;

    /// The water between `left` and `right`, textured `texture` and see-through, its picture running from the
    /// end of the water nearer the start of `track` (the piece's rails). Nothing when the edges do not pair up
    /// node for node or have no length. Its parts are marked `chain`, as their picture slides along them.
    [[nodiscard]] std::optional<ModelPart> flow_strip(const ovl::Spline& left, const ovl::Spline& right, const ovl::Spline& track, std::string texture);

    /// How far along its picture the water of a piece of run `run` (see Chains) is moved after flowing for
    /// `seconds`, from 0 up to 1, added to the picture's v: against the strip's v when the run is negative.
    [[nodiscard]] float flow_scroll(float seconds, std::int32_t run);

    /// The picture of the texture `name` (the flowing water's by default) from the overlay whose common file is
    /// `file`.
    [[nodiscard]] std::expected<ovl::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file, std::string_view name = kFlowTexture);

} // namespace rct3::park
