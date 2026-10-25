// SPDX-License-Identifier: GPL-3.0-or-later
//
// A plot of a few series of samples over the same span of time, each drawn as a line joining its samples in
// its colour, over a background ruled with level lines and marked along the bottom with ticks.
//
// Each series has its own scale: a round full value (1, 2 or 5 times a power of ten) at least as large as its
// largest sample either side of zero. A series that never goes below zero runs from zero at the bottom to its
// full value at the top; one that does has zero across the middle. The newest sample is at the right edge, and
// a plot with a fixed number of slots spaces samples one slot apart, so a short history fills only its right.
#pragma once

#include "ui/widget.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace rct3::ui
{

    struct PlotSeries
    {
        std::vector<float> values; ///< Oldest first
        Color colour = kWhite;
        float full = 0;       ///< Of its scale, top of the plot; 0 to work it out from its values
        bool centred = false; ///< Zero across the middle, for a series that goes below it
    };

    /// A mark along the bottom of a plot: `back` samples before the newest one, a longer mark when `major`.
    struct PlotTick
    {
        float back = 0;
        bool major = false;
    };

    class LinePlot : public Widget
    {
    public:
        explicit LinePlot(std::string name);

        std::vector<PlotSeries> series;
        std::vector<PlotTick> ticks;
        std::size_t slots = 0; ///< Samples across the plot; 0 to stretch the longest series over it
        Color background = hex("#00000030");
        Color rule_colour = hex("#7f7f7f60"); ///< Of the level lines and the ticks
        int rules = 8;                        ///< Spaces between level lines, top to bottom
        float line = 1.5f;                    ///< Width of the series' lines
        float tick = 4;                       ///< Height of a tick; a major one is twice as tall

        void draw(DrawContext& context) const override;

        /// Samples across the plot: its slots, or as many as the longest series has.
        [[nodiscard]] std::size_t span() const;
    };

    /// The full value of a scale for `values`: the smallest 1, 2 or 5 times a power of ten at least as large as
    /// the largest of them either side of zero. 1 for no values or only zeros.
    [[nodiscard]] float full_scale(std::span<const float> values);

    /// The values at the top, three quarters, half, a quarter and the bottom of a scale up to `full`.
    [[nodiscard]] std::array<float, 5> axis_values(float full, bool centred);

    /// Where in a plot `height` units tall a sample `value` sits, with y down, on a scale up to `full`: zero at
    /// the bottom, or in the middle for a `centred` scale that goes down to -full.
    [[nodiscard]] float plot_y(float value, float full, bool centred, float height);

    /// Where across a plot `width` units wide, spanning `span` samples, the sample `back` before the newest
    /// sits; the newest at the right edge.
    [[nodiscard]] float plot_x(float back, std::size_t span, float width);

} // namespace rct3::ui
