// SPDX-License-Identifier: GPL-3.0-or-later
//
// Particle skins: the picture a particle draws, as one or more rectangles of a texture atlas, how it blends,
// and how fast an animated one runs through its frames.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct ParticleSkin
    {
        std::string_view texture; ///< "tex" item the frames cut from. Empty when not referenced.
        /// Each frame's rectangle as left, top, right, bottom, 0 to 1 across the texture, top down.
        std::vector<std::array<float, 4>> frames;
        bool blended = false;  ///< Mixed by opacity; otherwise added to what is behind it (fire, sparks)
        bool animated = false; ///< Runs through its frames; otherwise each particle keeps one
        float loop_time = 1;   ///< Seconds for one run through the frames
    };

    struct ParticleSkinItem
    {
        std::string_view name;            ///< Symbol name without the tag
        std::optional<ParticleSkin> skin; ///< Nothing when the item or its frames are unreadable
    };

    /// Particle skin items of one part of an overlay: pointer symbols tagged "psi". Views in the result point
    /// into the overlay's bytes.
    [[nodiscard]] std::vector<ParticleSkinItem> read_particle_skins(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
