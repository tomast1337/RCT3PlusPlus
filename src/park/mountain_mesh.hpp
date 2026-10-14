// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hills and mountains on the horizon, round the park. The sky overlay holds them as one shape: a ring of
// picture panels standing round a centre, with the land in the lower part of each picture and clear sky
// above it. The shape has two meshes, each drawn with one of a pair of pictures for the park's landscape
// theme. Unlike the sky, the ring stays put round the middle of the map, so the camera moves against it.
#pragma once

#include "park/object_mesh.hpp"
#include "park/sky_mesh.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace rct3::park
{

    /// The ring's two meshes, each with its own picture.
    using MountainShapes = std::array<SkyShape, 2>;

    /// The landscape themes, in the order of the number a park file stores for its background.
    inline constexpr std::array<std::string_view, 8> kBackgroundThemes { "Mountain", "Desert", "Snow", "Hill", "Island", "Spooky", "Space", "City" };
    /// For a park that stores no background, or one out of range.
    inline constexpr std::size_t kDefaultBackground = 0;

    /// The theme for a park's stored background number.
    [[nodiscard]] std::string_view background_theme(std::optional<std::int32_t> stored);

    /// Names of the two pictures of `theme`, for mesh 0 and mesh 1.
    [[nodiscard]] std::array<std::string, 2> mountain_pictures(std::string_view theme);

    /// Beyond the map's farthest corner from its middle, how far the ring stands, in world units.
    inline constexpr float kMountainDistance = 900;

    struct MountainPlace
    {
        glm::vec3 centre { 0.0f }; ///< The ring's middle; the shape's height 0 lands at its height
        float radius = 1;          ///< How far the ring's panels are from the centre
    };

    /// Where the ring stands round `grid`: on the middle of the map at the height `ground`, out beyond its
    /// corners by kMountainDistance.
    [[nodiscard]] MountainPlace mountain_place(const save::TerrainGrid& grid, float ground);

    /// The ring placed at `place`, both meshes as cut-out batches. Empty when the shapes are.
    [[nodiscard]] ObjectMesh mountain_mesh(const MountainShapes& shapes, const MountainPlace& place);

} // namespace rct3::park
