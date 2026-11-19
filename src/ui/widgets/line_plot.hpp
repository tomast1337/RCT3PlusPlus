// SPDX-License-Identifier: GPL-3.0-or-later
//
// A plot of a few series of samples over the same span of time, each drawn as a line joining its samples in
// its colour, over a background ruled with level lines and marked along the bottom with ticks.
//
// Each series has its own scale: a round full value (1, 2 or 5 times a power of ten) at least as large as its
// largest sample either side of zero. A series that never goes below zero runs from zero at the bottom to its
// full value at the top; one that does has zero across the middle. The newest sample is at the right edge, and
// a plot with a fixed number of slots spaces samples one slot apart, so a short history fills only its right.
// The wheel zooms a plot that allows it in on its newest samples and out again, and a rule marks the sample under
// the pointer.
#pragma once

#include "core/types.hpp"
#include "ui/widget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::ui::widgets
{

    struct PlotSeries
    {
        std::vector<f32> values; ///< Oldest first
        Color colour = kWhite;
        f32 full = 0;         ///< Of its scale, top of the plot; 0 to work it out from its values
        bool centred = false; ///< Zero across the middle, for a series that goes below it
    };

    /// A mark along the bottom of a plot: `back` samples before the newest one, a longer mark when `major`.
    struct PlotTick
    {
        f32 back = 0;
        bool major = false;
    };

    class LinePlot : public Widget
    {
    public:
        explicit LinePlot(std::string name);

        std::vector<PlotSeries> series;
        std::vector<PlotTick> ticks;
        usize slots = 0; ///< Samples across the plot; 0 to stretch the longest series over it
        Color background = hex("#00000030");
        Color rule_colour = hex("#7f7f7f60"); ///< Of the level lines and the ticks
        i32 rules = 8;                        ///< Spaces between level lines, top to bottom
        f32 line = 1.5f;                      ///< Width of the series' lines
        f32 tick = 4;                         ///< Height of a tick; a major one is twice as tall
        usize most_slots = 0;                 ///< The wheel zooms out to this many slots at most; 0 for no zooming
        usize fewest_slots = 4;               ///< And in to this many at least

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void on_hover(Point p) override;
        bool on_wheel(Point notches) override;
        void draw(DrawContext& context) const override;

        /// The sample under the pointer, by how many samples before the newest; none when it is not over the plot.
        [[nodiscard]] std::optional<usize> pointed() const;
        /// The slots the wheel zoomed to since the last call.
        [[nodiscard]] std::optional<usize> take_zoom()
        {
            return std::exchange(zoomed_, std::nullopt);
        }

        /// Samples across the plot: its slots, or as many as the longest series has.
        [[nodiscard]] usize span() const;

    private:
        usize under_ = 0;
        std::optional<usize> zoomed_;
    };

    /// The full value of a scale for `values`: the smallest 1, 2 or 5 times a power of ten at least as large as
    /// the largest of them either side of zero. 1 for no values or only zeros.
    [[nodiscard]] f32 full_scale(std::span<const f32> values);

    /// The values at the top, three quarters, half, a quarter and the bottom of a scale up to `full`.
    [[nodiscard]] std::array<f32, 5> axis_values(f32 full, bool centred);

    /// Where in a plot `height` units tall a sample `value` sits, with y down, on a scale up to `full`: zero at
    /// the bottom, or in the middle for a `centred` scale that goes down to -full.
    [[nodiscard]] f32 plot_y(f32 value, f32 full, bool centred, f32 height);

    /// Where across a plot `width` units wide, spanning `span` samples, the sample `back` before the newest
    /// sits; the newest at the right edge.
    [[nodiscard]] f32 plot_x(f32 back, usize span, f32 width);

} // namespace rct3::ui::widgets
