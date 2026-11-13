// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ghosts: the see-through objects the build tools show where a piece, path or structure would go, tinted by
// whether it can be built there, and drawn glowing at any hour.
#pragma once

#include "park/object/mesh.hpp"
#include "render/device.hpp"

#include <glm/glm.hpp>

namespace rct3::scene
{

    namespace park
    {
        struct ParkScene;
    }

    /// Paints and uploads the pictures of `ghost` the scene does not have yet.
    void ghost_textures(render::Device& device, park::ParkScene& scene, const rct3::park::object::ObjectMesh& ghost);

    /// Ghosts' tints: bluish where the piece can be built, red where it cannot.
    inline constexpr glm::vec4 kGhostTint { 0.45f, 0.75f, 1.0f, 0.7f };
    inline constexpr glm::vec4 kRefusedTint { 1.0f, 0.2f, 0.15f, 0.7f };

    /// `light` as ghosts are drawn in: their own colour, unlit, with no haze, pulsing gently with `clock` (seconds),
    /// so they glow at any hour.
    [[nodiscard]] render::SceneLight ghost_light(render::SceneLight light, float clock);
    /// Draws `mesh` see-through with `transform` and `light`; `solid`, as a built object is drawn instead, for a
    /// picture of it alone.
    void draw_ghost_mesh(render::Device& device, const park::ParkScene& scene, const rct3::park::object::ObjectMesh& mesh, const glm::mat4& transform, const render::SceneLight& light, bool solid = false);

} // namespace rct3::scene
