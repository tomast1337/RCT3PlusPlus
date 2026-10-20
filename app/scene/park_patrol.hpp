// SPDX-License-Identifier: GPL-3.0-or-later
//
// Staff patrol areas as the player sees and paints them. While a member of staff the simulation walks is shown
// in the info panel, the tiles of their patrol area are tinted over the ground, and other staff's areas
// fainter in their job's colour. With painting ticked on their tasks page, a stroke on the ground (the left
// button held and dragged) paints a square of tiles the brush's size: it adds them, or takes them away when
// the stroke started on a tile already in.
#pragma once

#include "render/device.hpp"
#include "simulation/entt.hpp"
#include "ui/widget.hpp"

#include <glm/glm.hpp>

namespace rct3::scene
{

    struct ParkScene;

    /// The simulated member of staff the info panel shows, or null.
    [[nodiscard]] entt::entity shown_staff(const ParkScene& scene);

    /// Whether the left button on the park goes to painting the patrol area, rather than picking.
    [[nodiscard]] bool painting_patrol(const ParkScene& scene);

    /// Paints with the pointer at `at` (interface units, on a view `size` across), `down` while the left button is
    /// held over the park. Returns whether a stroke just ended, so the page shows the area's new size.
    bool paint_patrol(ParkScene& scene, ui::Point at, ui::Point size, bool down);

    /// Gives the shown member of staff's area to everyone else in their job.
    void copy_patrol(ParkScene& scene);

    /// Tints the shown member of staff's patrol area over the ground.
    void draw_patrol(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
