// SPDX-License-Identifier: GPL-3.0-or-later
//
// Running a particle effect (particles/effect.hpp) at a place in the park. Time moves in fixed steps, so the
// same seed and the same calls give the same particles. Each emitter sends its particles out evenly over its
// share of its owner's life; the owner is the run itself for the effect's own emitters, or a particle for a
// particle's emitters. Particles fall with gravity, slow with drag and take their colour and size from their
// curves.
#pragma once

#include "math/glm.hpp"
#include "particles/effect.hpp"
#include "random/generator.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rct3::particles
{

    /// A run's live particles, one array per field, all as long as `count()`, so a pass over one field (moving
    /// them, uploading them) reads only that field.
    struct ParticleArrays
    {
        std::vector<glm::vec3> position { };
        std::vector<glm::vec3> velocity { };
        /// Seconds; below 0 it is not born yet (an emitter's time spread) and is not drawn
        std::vector<float> age { };
        std::vector<float> life { };
        std::vector<float> scale { };           ///< Its share of the particle's size, from the size spread
        std::vector<float> roll { };            ///< Radians on screen
        std::vector<std::uint32_t> kind { };    ///< In Effect::particles
        std::vector<std::uint8_t> relative { }; ///< Moves along when the run is moved

        [[nodiscard]] std::size_t count() const noexcept
        {
            return age.size();
        }
    };

    /// How a live particle looks now: its colour and opacity, and its size in world units.
    struct Look
    {
        glm::vec4 colour { 1.0f };
        float size = 1;
    };

    /// How particle `i` of `particles` looks now.
    [[nodiscard]] Look look(const Effect& effect, const ParticleArrays& particles, std::size_t i);

    /// Steps a second.
    inline constexpr float kParticleSteps = 60;

    class EffectRun
    {
    public:
        /// `effect` must outlive the run. A looping run starts the effect again each time its life ends;
        /// otherwise it ends once the effect and its last particle have. `strength` 0 to 1 scales the emitters
        /// whose count follows it.
        EffectRun(const Effect& effect, random::RandomState random, bool loops, std::size_t capacity, float strength = 1);

        /// Where the run is: position, its axes (columns; the second is its up) and its speed. Particles
        /// that move with it are carried along.
        void place(glm::vec3 position, const glm::mat3& axes = glm::mat3(1.0f), glm::vec3 velocity = glm::vec3(0.0f));

        /// Moves time on by `seconds`, in whole fixed steps; the rest waits for the next call.
        void advance(float seconds);

        [[nodiscard]] const ParticleArrays& particles() const noexcept
        {
            return live_;
        }
        [[nodiscard]] bool finished() const noexcept
        {
            return !loops_ && age_ >= life_ && live_.count() == 0;
        }

    private:
        void step(float dt);
        /// Emits what `emitter` owes once its owner is `age` into its `life`.
        void emit(const Emitter& emitter, float& emitted, float age, float life, glm::vec3 position, glm::vec3 velocity);

        const Effect* effect_;
        random::RandomState random_;
        bool loops_;
        std::size_t capacity_;
        float strength_;
        glm::vec3 position_ { 0.0f };
        glm::mat3 axes_ { 1.0f };
        glm::vec3 velocity_ { 0.0f };
        float age_ = 0;
        float life_ = 1;
        float pending_ = 0;
        std::vector<float> root_emitted_;
        ParticleArrays live_;
        /// Per pool slot, how many each emitter of the slot's particle has sent: `widest_` counters a slot.
        std::vector<float> emitted_;
        std::size_t widest_ = 0;
    };

} // namespace rct3::particles
