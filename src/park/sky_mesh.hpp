// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sky round the park, built again each frame around the viewer. It is a set of domes: one coloured
// from a gradient picture, stars and the milky way for the night, and clouds drawn twice at two sizes
// drifting at different speeds, so the two layers slide over each other. Every dome is centred on the eye
// and keeps its distance as the camera moves, so the sky never shows an edge however far the view goes.
//
// The coloured dome's picture runs through the day across and from the horizon up the sky down its side:
// the dome reads one column of it, the one for the time of day.
#pragma once

#include "park/object_mesh.hpp"
#include "park/object_models.hpp"

#include <string>
#include <vector>

namespace rct3::park
{

    struct SkyShape
    {
        std::string texture; ///< Key of the picture it is drawn with
        std::vector<ModelVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    struct SkyModels
    {
        SkyShape dome;
        SkyShape stars;
        SkyShape milky_way;
        SkyShape clouds;
    };

    /// Where the column of the colour picture for the middle of the day is, across it.
    inline constexpr float kMiddayColumn = 0.3f;
    /// How opaque the clouds are at most.
    inline constexpr float kCloudCover = 0.65f;
    /// How far each cloud layer slides per second, in picture widths.
    inline constexpr float kCloudDrift = 0.003f;
    inline constexpr float kHighCloudDrift = 0.0045f;

    struct SkyView
    {
        glm::vec3 eye { 0.0f };
        float radius = 1;                ///< How far the farthest point of the sky is from the eye
        float day = kMiddayColumn;       ///< Column of the colour picture
        float faded_day = kMiddayColumn; ///< Column of a look fading out over it
        float fade = 0;                  ///< How much of the fading look still shows, 0 for none
        float night = 0;                 ///< 0 by day, up to 1 when the stars show fully
        float time = 0;                  ///< Seconds, for the clouds' drift
        glm::vec3 tint { 1.0f };         ///< Times the sky's colours, for the weather
        float overcast = 0;              ///< 0 the usual clouds, up to 1 when they cover the sky dark and hide the stars
    };

    /// The sky seen from `view`, in drawing order: the coloured dome (opaque), the dome again in the fading
    /// look while one fades out, then stars and milky way when it is night, then both cloud layers (blended).
    /// Shapes without vertices are left out.
    [[nodiscard]] ObjectMesh sky_mesh(const SkyModels& sky, const SkyView& view);

} // namespace rct3::park
