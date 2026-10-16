// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's water on the GPU. Where the device can, it is drawn with the water shader: the scene mirrored in
// it (drawn again into a half-size picture for each of the few heights of water that cover most of the view),
// what lies under it (the opaque scene copied from the screen, with its depth), waves from a tiling height
// picture, the ripples a click starts and a faint foam. Otherwise, and in the info panel's small round view,
// it is drawn as before: a lit, see-through surface.
//
// A frame with water goes:
//
//   prepare_park_water   between passes: the ripples are sent, the reflections drawn
//   (the opaque scene)
//   draw_park_water      the screen is copied, then the water drawn over it
//   (see-through objects, the interface)
#pragma once

#include "config/game_options.hpp"
#include "park/camera.hpp"
#include "park/terrain_mesh.hpp"
#include "park/water_ripples.hpp"
#include "park/water_surface.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"

#include <array>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace rct3::scene
{

    /// At most this many heights of water get a reflection of their own each frame.
    inline constexpr std::size_t kWaterReflections = 2;

    /// What the game options ask of the water.
    struct WaterLook
    {
        bool reflection = true; ///< The scene mirrored in it; else only the sky's colour
        bool refraction = true; ///< What lies under it, bent and fading with depth; else it is blended over it
        bool waves = true;
        bool reflect_background = true; ///< Sky and mountains
        bool reflect_terrain = true;
        bool reflect_scenery = true;
        bool reflect_people = false;
    };

    [[nodiscard]] WaterLook water_look(const config::GameOptions& options);

    struct ParkWater
    {
        park::WaterSurface surface;
        render::BufferHandle vertices;
        render::PipelineHandle shaded; ///< The water shader; none where the device has not got it
        render::PipelineHandle simple; ///< Lit and see-through
        render::TextureHandle waves;   ///< Tiling height picture
        render::TextureHandle white;   ///< For the simple surface, which the picture tints
        render::TextureHandle ripple_texture;
        std::array<render::TextureHandle, kWaterReflections> reflections { };
        std::array<render::Extent, kWaterReflections> reflection_sizes { };
        render::TextureHandle behind; ///< The opaque scene and its depth, copied from the screen
        render::Extent behind_size;
        bool have_behind = false; ///< The copy this frame worked
        park::Ripples ripples;
        WaterLook look;
        std::vector<float> levels; ///< With a reflection this frame, by index in `reflections`
    };

    /// Makes the water's buffers, pictures and pipelines. `layout` is that of park::GroundVertex.
    [[nodiscard]] std::expected<void, std::string> create_park_water(render::Device& device, ParkWater& water, std::span<const park::WaterVertex> mesh, const render::VertexLayout& layout);
    void destroy_park_water(render::Device& device, ParkWater& water);

    /// Ages the ripples.
    void advance_park_water(ParkWater& water, float seconds);

    /// Starts a ripple where `ray` meets the water, unless the ground is in front of it. True if it did.
    bool ripple_park_water(ParkWater& water, const park::Ray& ray, const save::TerrainGrid& grid);

    /// Draws the world mirrored by the transform given, without its water.
    using MirroredDraw = std::function<void(const glm::mat4& transform)>;

    /// Call inside the screen pass before anything of the world is drawn; it ends the pass and begins it again
    /// keeping what was drawn.
    void prepare_park_water(render::Device& device, ParkWater& water, const glm::mat4& view_projection, glm::vec3 eye, render::Color sky, const MirroredDraw& mirrored);

    /// Call inside the screen pass after the opaque world, before what is see-through. With `shaded` false
    /// (the info panel's view, or no water shader) it draws the simple surface only.
    void draw_park_water(render::Device& device, ParkWater& water, const glm::mat4& transform, const render::SceneLight& light, glm::vec3 eye, float time, bool shaded);

} // namespace rct3::scene
