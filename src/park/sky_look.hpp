// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the sky looks like at a time of day, worked out from where the sun is: which column of the coloured
// dome's picture shows, and how dark it is, which is how strongly the stars come through.
//
// The picture runs through a day across it, and its two edges are the same deep night, so it may be read
// round and round. Going right from the left edge: night, the glow of dawn, a pale early morning, the clear
// blue of the middle of the day, then the warm colours of sunset and back into night. The columns below were
// measured where those looks are. The morning walks the left part from night to midday as the sun climbs,
// the evening the right part from midday back to night as it sinks: the sun's height picks the column, so
// dawn and dusk come when the sun is low, whatever the hour.
#pragma once

#include "math/glm.hpp"

#include <span>

namespace rct3::park
{

    /// Columns of the dome's picture, across it, as measured.
    inline constexpr float kNightColumn = 0.0f;  ///< Also the right edge
    inline constexpr float kDawnColumn = 0.044f; ///< The warmest of the dawn glow
    inline constexpr float kMorningColumn = 0.07f;
    inline constexpr float kAfternoonColumn = 0.6f; ///< Blue going grey as the sun gets low
    inline constexpr float kSunsetColumn = 0.72f;
    inline constexpr float kDuskColumn = 0.82f;
    inline constexpr float kLateDuskColumn = 0.9f; ///< Night again, but for a trace of blue

    /// The sun's height in degrees where the stars start to show and where they show fully.
    inline constexpr float kStarsAppear = 3.0f;
    inline constexpr float kStarsFull = -10.0f;

    /// The column of the dome's picture for `time`, a fraction of a day from midnight.
    [[nodiscard]] float sky_column(float time);

    /// How dark the sky is at `time`: 0 while the sun is well up, 1 once it is well below the horizon.
    [[nodiscard]] float sky_darkness(float time);

    /// The colour of `band` (one colour per column of the picture, left to right) at `column`, the nearest
    /// one, reading the band round. Transparent black for an empty band.
    [[nodiscard]] glm::vec4 band_colour(std::span<const glm::vec4> band, float column);

} // namespace rct3::park
