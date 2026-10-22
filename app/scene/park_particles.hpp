// SPDX-License-Identifier: GPL-3.0-or-later
//
// The particle effects scenery and rides show (smoke, fire, spray), run and drawn. Each effect a placed
// object's model marks runs on its own, looping; those far from the eye wait. Particles are drawn after the
// opaque world as quads facing the eye, cut from the particle atlases: fire and sparks added to what is
// behind them, smoke and spray mixed over it, farthest first.
#pragma once

#include "park/object_mesh.hpp"
#include "particles/library.hpp"
#include "particles/simulation.hpp"
#include "random/generator.hpp"
#include "render/device.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::scene
{

    struct ParkParticles
    {
        struct Shown
        {
            particles::EffectRun run;
            /// By the effect's particle kinds: the sprite each draws, or none.
            std::vector<const particles::Sprite*> sprites;
            glm::vec3 position { 0.0f };
            std::string effect { }; ///< Its file's name
            bool awake = false;     ///< Ran in the last advance, so it is drawn
        };

        std::optional<particles::Library> library;
        std::vector<Shown> shown;
        std::vector<Shown> fired; ///< Effects set off once, such as fireworks; dropped when they end
        random::RandomState random;
        std::map<std::string, render::TextureHandle> atlases;
        render::PipelineHandle added;
        render::PipelineHandle mixed;
    };

    /// Loads the particle sprites of the install at `root`, with numbers from `random`. Logs and leaves the library
    /// out when the install has no particle sprites; nothing is shown then.
    void load_park_particles(render::Device& device, ParkParticles& particles, const std::filesystem::path& root, random::RandomState random);
    /// Starts the effects `effects` name, in place of any running.
    void show_park_particles(ParkParticles& particles, std::span<const park::PlacedEffect> effects);
    /// Sets off the effect `effect` once at `position`, pointing along `up`. False when there is no such effect.
    bool fire_park_particles(ParkParticles& particles, std::string_view effect, glm::vec3 position, glm::vec3 up);
    /// Moves time on for the effects near enough `eye` to be seen, nearest first, until `budget` particles are
    /// alive; the rest wait and are not drawn.
    void advance_park_particles(ParkParticles& particles, float seconds, glm::vec3 eye, std::size_t budget);
    /// Draws them into the open pass, lit by `light` (smoke dims at night; fire does not).
    void draw_park_particles(render::Device& device, const ParkParticles& particles, const glm::mat4& view_projection, glm::vec3 eye, glm::vec3 target, const render::SceneLight& light);
    void destroy_park_particles(render::Device& device, ParkParticles& particles);

} // namespace rct3::scene
