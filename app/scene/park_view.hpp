// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the view menu adds over the park: the tile grid and contours on the ground, and a label of the height
// over each path or piece of track near the middle of the view. The labels stay on until switched off, while
// building or just watching the park.
#pragma once

#include "render/device.hpp"
#include "ui/widgets/world_labels.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    /// Makes the grid and contours the view menu shows, and drops those it hides.
    void update_view_lines(render::Device& device, ParkScene& scene);

    /// Draws the grid and contours over the ground.
    void draw_view_lines(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    /// Drops the lines' buffers.
    void drop_view_lines(render::Device& device, ParkScene& scene);

    /// The height labels the view menu shows, on a view `size` across (interface units), farthest first.
    [[nodiscard]] std::vector<ui::WorldLabel> height_labels(const ParkScene& scene, ui::Point size);

} // namespace rct3::scene
