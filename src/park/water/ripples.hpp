// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rings spreading over the water from where the player clicked it. Each ripple is a short train of waves
// round a ring that widens at a steady speed, and dies down as it ages and as it spreads. The shape is ours:
//
//   height(r) = amplitude * exp(-(x / width)^2) * cos(wave_number * x),  x = r - radius
//
// where r is the distance from the ripple's middle and radius the ring's. A few ripples are kept at a time;
// a new one takes the place of the oldest when all are in use. The renderer is handed each ripple's middle,
// radius, amplitude, wave number and width (ripple_texels) and evaluates the shape itself, for its slopes and
// crests.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>

namespace rct3::park::water
{

    inline constexpr usize kMaxRipples = 16;

    /// How ripples look and behave. Lengths in world units (about a metre), times in seconds.
    struct RippleLook
    {
        f32 speed = 3.0f;             ///< Of the ring, outwards
        f32 wavelength = 1.2f;        ///< From one crest to the next
        f32 width = 1.6f;             ///< Of the train of waves round the ring
        f32 height = 0.07f;           ///< Of the crests when the ripple starts
        units::Seconds fade { 2.2f }; ///< For the height to fall to a third (1/e)
        f32 spread = 2.0f;            ///< The height also falls as the ring grows past this radius
        f32 gone = 0.02f;             ///< A ripple whose height has fallen below this share of `height` ends
    };
    inline constexpr RippleLook kRippleLook { };

    struct Ripple
    {
        glm::vec2 middle { 0.0f }; ///< World x and z
        f32 age = 0;
        f32 strength = 0; ///< 0 for a free place, up to 1
    };

    struct Ripples
    {
        std::array<Ripple, kMaxRipples> ripples { };
    };

    /// Starts a ripple at `middle`, in a free place or else in place of the oldest.
    void start_ripple(Ripples& ripples, glm::vec2 middle, f32 strength = 1, const RippleLook& look = kRippleLook);
    /// Ages the ripples by `seconds`, ending those that have died down.
    void advance_ripples(Ripples& ripples, f32 seconds, const RippleLook& look = kRippleLook);
    [[nodiscard]] usize live_ripples(const Ripples& ripples);

    [[nodiscard]] f32 ripple_radius(const Ripple& ripple, const RippleLook& look = kRippleLook);
    /// The height of its crests now; 0 for a free place.
    [[nodiscard]] f32 ripple_amplitude(const Ripple& ripple, const RippleLook& look = kRippleLook);
    /// How far the water stands above its level at world x and z `at`, from all the ripples together.
    [[nodiscard]] f32 ripple_height(const Ripples& ripples, glm::vec2 at, const RippleLook& look = kRippleLook);

    /// What the renderer reads, two rows of kMaxRipples: the first holds each ripple's middle x and z, radius and
    /// amplitude (0 for none), the second its wave number (radians per world unit) and width.
    using RippleTexels = std::array<glm::vec4, 2 * kMaxRipples>;
    [[nodiscard]] RippleTexels ripple_texels(const Ripples& ripples, const RippleLook& look = kRippleLook);

} // namespace rct3::park::water
