// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water running down slides and water rides. A track piece that carries water names two more splines,
// the water's left and right edges, in the same space as its rails. The water is a strip between them,
// sampled a few times along each part of the splines. Its picture runs once across the strip and repeats
// along it a whole number of times, so pieces laid end to end join without a seam. Its picture runs the way
// the piece's track runs, whichever end the file lists its water from, and slides that way as the water flows;
// pieces laid against their section's own way slide it the other way round (flow_scroll).
//
// Some water rides' pieces (a water coaster's splash troughs) name no water edges: their model carries the
// water as a see-through sheet of its own, its picture already running once across and on along the piece.
// That sheet is given the same flowing water (flowing_sheet).
#pragma once

#include "math/texture_shift.hpp"
#include "ovl/spline.hpp"
#include "ovl/texture_image.hpp"
#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "save/model/rides.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
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

    /// Makes `sheet`, a piece's own see-through water, flowing water textured `texture` as flow_strip's are, its
    /// picture turned round where it runs from the far end of `track` (the piece's rails).
    void flowing_sheet(ModelPart& sheet, const ovl::Spline& track, std::string texture);

    /// The fades of every ride's water pieces among `objects` drawn with `models`; `pool_slides` says by ride
    /// which are pool slides, whose last piece is an open end when their track is not a circuit.
    [[nodiscard]] FlowFades flow_fades(const save::RideModel& rides, std::span<const save::PlacedObject> objects, const ObjectModels& models, const std::function<bool(std::size_t)>& pool_slides);

    /// How much of the water shows (0 to 1) at `along` its strip (ModelPart::flow) faded by `fade`, `last` being
    /// the strip's ModelPart::flow_last.
    [[nodiscard]] float flow_alpha(const FlowFade& fade, float along, float last);

    /// How far along its picture the water of a piece of run `run` (see Chains) is moved after flowing for
    /// `seconds`, added to the picture's v: against the strip's v when the run is negative.
    [[nodiscard]] TextureShift flow_scroll(float seconds, std::int32_t run);

    /// The picture of the texture `name` (the flowing water's by default) from the overlay whose common file is
    /// `file`.
    [[nodiscard]] std::expected<ovl::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file, std::string_view name = kFlowTexture);

} // namespace rct3::park
