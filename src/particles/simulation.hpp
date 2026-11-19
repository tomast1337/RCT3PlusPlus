// SPDX-License-Identifier: GPL-3.0-or-later
//
// Running a particle effect (particles/effect.hpp) at a place in the park. Time moves in fixed steps, so the
// same seed and the same calls give the same particles. Each emitter sends its particles out evenly over its
// share of its owner's life; the owner is the run itself for the effect's own emitters, or a particle for a
// particle's emitters. Particles fall with gravity, slow with drag and take their colour and size from their
// curves. A sound the effect names starts as the run does, and one a kind of particle names as such a particle is
// born, where it is born: a shell's bang is heard where it bursts.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "particles/effect.hpp"
#include "particles/library.hpp"
#include "random/generator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
        std::vector<f32> age { };
        std::vector<f32> life { };
        std::vector<f32> scale { };   ///< Its share of the particle's size, from the size spread
        std::vector<f32> roll { };    ///< Radians on screen
        std::vector<u32> kind { };    ///< In Effect::particles
        std::vector<u8> relative { }; ///< Moves along when the run is moved

        [[nodiscard]] usize count() const noexcept
        {
            return age.size();
        }
    };

    /// How a live particle looks now: its colour and opacity, and its size in world units.
    struct Look
    {
        glm::vec4 colour { 1.0f };
        f32 size = 1;
        glm::vec3 stretch { 0.0f }; ///< How much longer it is drawn along its motion, as a world vector
    };

    /// How particle `i` of `particles` looks now.
    [[nodiscard]] Look look(const Effect& effect, const ParticleArrays& particles, usize i);

    /// The light glowing particles throw on what is round them: the sum of their colours, from their middle as
    /// weighted by how bright each is.
    struct Glow
    {
        glm::vec3 position { 0.0f };
        glm::vec3 colour { 0.0f };
    };

    /// Most particles `glow` looks at in one run; it scales the rest up from them.
    inline constexpr usize kGlowSamples = 256;

    /// The glow of `particles`, each kind drawn with `sprites` (by Effect::particles): the added ones glow (fire,
    /// sparks), the mixed ones do not (smoke, spray). Nothing once they have faded or died.
    [[nodiscard]] Glow glow(const Effect& effect, const ParticleArrays& particles, std::span<const Sprite* const> sprites);

    /// A sound that began: the effect's own, or particle kind `particle`'s.
    struct Heard
    {
        glm::vec3 position { 0.0f };
        std::optional<usize> particle { }; ///< In Effect::particles; none for the effect's own
    };

    /// Steps a second.
    inline constexpr f32 kParticleSteps = 60;

    class EffectRun
    {
    public:
        /// `effect` must outlive the run. A looping run starts the effect again each time its life ends;
        /// otherwise it ends once the effect and its last particle have. `strength` 0 to 1 scales the emitters
        /// whose count follows it. `scale` shrinks or grows the whole effect in space, keeping its timing: how far
        /// its particles go and how fast, and how far they fall (their sizes are the drawing's, Look::size).
        EffectRun(const Effect& effect, random::RandomState random, bool loops, usize capacity, f32 strength = 1, f32 scale = 1);

        /// Where the run is: position, its axes (columns; the second is its up) and its speed. Particles
        /// that move with it are carried along.
        void place(glm::vec3 position, const glm::mat3& axes = glm::mat3(1.0f), glm::vec3 velocity = glm::vec3(0.0f));

        /// Moves time on by `seconds`, in whole fixed steps; the rest waits for the next call.
        void advance(f32 seconds);

        [[nodiscard]] const Effect& effect() const noexcept
        {
            return *effect_;
        }
        [[nodiscard]] const ParticleArrays& particles() const noexcept
        {
            return live_;
        }
        /// The sounds that began since forget_sounds, earliest first; one a kind of particle a step.
        [[nodiscard]] const std::vector<Heard>& sounds() const noexcept
        {
            return heard_;
        }
        void forget_sounds() noexcept
        {
            heard_.clear();
        }
        [[nodiscard]] bool finished() const noexcept
        {
            return !loops_ && age_ >= life_ && live_.count() == 0;
        }

    private:
        void step(f32 dt);
        /// Emits what `emitter`, having sent `emitted`, owes once its owner is `age` into its `life`; returns how
        /// many it has sent then. Taken by value: emitting can move the counters.
        f32 emit(const Emitter& emitter, f32 emitted, f32 age, f32 life, glm::vec3 position, glm::vec3 velocity);

        const Effect* effect_;
        random::RandomState random_;
        bool loops_;
        usize capacity_;
        f32 strength_;
        f32 scale_;
        glm::vec3 position_ { 0.0f };
        glm::mat3 axes_ { 1.0f };
        glm::vec3 velocity_ { 0.0f };
        f32 age_ = 0;
        f32 life_ = 1;
        f32 pending_ = 0;
        std::vector<f32> root_emitted_;
        ParticleArrays live_;
        /// Per pool slot, how many each emitter of the slot's particle has sent: `widest_` counters a slot.
        std::vector<f32> emitted_;
        usize widest_ = 0;
        std::vector<Heard> heard_;
    };

} // namespace rct3::particles
