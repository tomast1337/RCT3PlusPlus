// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's waterfalls on the GPU: their mesh (park/waterfall_mesh.hpp) in buffers of its own, built again only
// when the falls, their lakes or the Waterfall detail option change, and drawn with the waterfall shader
// (app/shaders/waterfall.slang), which runs the water from the park's clock.
#pragma once

#include "park/water/surface.hpp"
#include "park/waterfall_mesh.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "scene/park/water.hpp"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::scene::park
{

    struct ParkWaterfalls
    {
        rct3::park::WaterfallMesh mesh;
        std::vector<rct3::park::FallLakes> lakes; ///< By fall, as the mesh was built
        render::BufferHandle vertices;
        render::BufferHandle indices;
        render::PipelineHandle pipeline; ///< None where the device has not got the waterfall shader
        rct3::park::WaterfallDetail detail = rct3::park::WaterfallDetail::High;
    };

    /// How the falls are seen in a draw.
    enum class FallsSeen : std::uint8_t
    {
        Direct,         ///< In a view of the park, without the scene behind them
        WithScene,      ///< In the park's own view, which copied the scene behind the water (ParkWater::behind)
        Mirrored,       ///< In the lake's reflection: the sheet alone, from High
        FromUnderWater, ///< From under the lake: the sheet alone, seen through the surface
    };

    /// Makes the falls' pipeline; without the shader on the device they are not drawn, which is no failure.
    /// `layout` is that of rct3::park::GroundVertex.
    void create_park_waterfalls(render::Device& device, ParkWaterfalls& falls, const render::VertexLayout& layout);
    /// Builds the falls' mesh again for `park_falls` pouring between the lakes of `lakes`, at `falls.detail`.
    void refresh_park_waterfalls(render::Device& device, ParkWaterfalls& falls, std::span<const save::model::Waterfall> park_falls, const rct3::park::water::WaterSurface& lakes);
    /// Draws them into the open pass through `transform`, seen from `eye` (mirrored with the transform, in a
    /// reflection), lit by `light`, at `time` of the park's clock; the wave picture and the scene behind are
    /// `water`'s.
    void draw_park_waterfalls(render::Device& device, const ParkWaterfalls& falls, const ParkWater& water, const glm::mat4& transform, const render::SceneLight& light, glm::vec3 eye, float time, FallsSeen seen);
    void destroy_park_waterfalls(render::Device& device, ParkWaterfalls& falls);

} // namespace rct3::scene::park
