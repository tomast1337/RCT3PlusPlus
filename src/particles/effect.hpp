// SPDX-License-Identifier: GPL-3.0-or-later
//
// Particle effects: fire, smoke, spray, fireworks. An effect file is the save container (save/save_file.hpp)
// holding three kinds of object:
//  - the effect itself: how long one round of it lasts, a tint, a sound, and its emitters;
//  - emitters: when in their owner's life they emit, how many particles, how fast and how scattered;
//  - particles: how long they live, their size, how gravity and drag pull them, their picture (a particle
//    skin, ovl/particle_skin.hpp), colour curves over their life, and their own emitters, so a particle can
//    leave a trail of others.
// Objects refer to each other by id, and several may share one; here they become indices.
#pragma once

#include "math/glm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::particles
{

    /// A value over a particle's life: `values` at `times` (0 at birth, 1 at death), straight between them.
    struct Curve
    {
        std::vector<float> times { };
        std::vector<float> values { };
        /// Past the last time, run on to the first value as if the times went round again.
        bool wrap = false;

        /// The value at `t`, 0 to 1: the first value before the first time, the last after the last (or towards
        /// the first, when it wraps); 1 when there are no values.
        [[nodiscard]] float at(float t) const noexcept;
    };

    struct Emitter
    {
        float start = 0;       ///< When in its owner's life it starts emitting, 0 to 1
        float end = 1;         ///< And stops
        float time_random = 0; ///< Each particle's birth is moved by up to this share of the owner's life
        std::int32_t count = 0;
        bool count_scaled = false;               ///< The count follows the effect's strength
        float speed = 0;                         ///< Along the owner's up, world units a second
        float speed_random = 0;                  ///< Up to this added along up
        float speed_circle = 0;                  ///< Up to this added across up, in any direction
        float position_random = 0;               ///< Born up to this far from the owner, in any direction
        float position_circle = 0;               ///< Born up to this far from the owner, across its up
        float parent_speed = 0;                  ///< Share of the owner's own speed the particles keep
        bool relative = false;                   ///< Particles keep moving with their owner
        glm::vec3 spin_rate { 0.0f };            ///< The emitter turns round x, y and z, radians a second
        glm::vec3 spin_offset { 0.0f };          ///< Starting at these angles
        std::optional<std::size_t> particle { }; ///< What it emits, in Effect::particles
    };

    struct Particle
    {
        float lifetime = 1;
        float lifetime_random = 0; ///< Up to this share of the lifetime taken off
        float start_size = 1;
        float end_size = 1;
        float size_random = 0; ///< Up to this share of the size taken off
        float gravity = 0;     ///< Times the park's gravity; below 0 it rises
        float drag = 0;        ///< Share of its speed lost a second
        float stretch = 0;     ///< Drawn longer along its motion by this times its speed
        float up_scale = 0;    ///< Drawn taller by this
        float roll = 0;        ///< Turns on screen, radians a second
        glm::vec2 spin_rate { 0.0f };
        std::int32_t burn_length = 0;    ///< A trail of this many copies behind it
        float burn_size = 0;             ///< Their size
        std::string skin { };            ///< Its picture: a particle skin's name
        std::array<Curve, 4> colour { }; ///< Red, green, blue and opacity over its life
        bool tinted = false;             ///< Takes the effect's tint
        std::string sound { };
        float sound_near = 0;                  ///< Heard fully within this
        float sound_far = 0;                   ///< And not at all past this
        std::vector<std::size_t> emitters { }; ///< In Effect::emitters
    };

    struct Effect
    {
        float lifetime = 1;        ///< One round of it, in seconds; a lasting effect starts it again
        float lifetime_random = 0; ///< Up to this share of the lifetime taken off
        glm::vec4 tint { 1.0f };
        float strength = 1;
        std::string sound { };
        float sound_near = 0;
        float sound_far = 0;
        std::vector<std::size_t> roots { }; ///< Its own emitters, in `emitters`
        std::vector<Emitter> emitters { };
        std::vector<Particle> particles { };
    };

    enum class EffectError : std::uint8_t
    {
        Container,   ///< Not a readable save container
        NoEffect,    ///< It holds no effect object
        BadObject,   ///< An object of the effect could not be decoded
        Unreferable, ///< A reference names no object of the kind it should
    };

    [[nodiscard]] const char* to_string(EffectError e) noexcept;

    /// The effect in an effect file. Attributes missing from older files take their defaults above.
    [[nodiscard]] std::expected<Effect, EffectError> read_effect(std::span<const std::byte> data);

} // namespace rct3::particles
