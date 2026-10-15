// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small plot of a few series of samples over the same span, each stretched to the full height between its
// own lowest and highest sample and drawn as a trail of dots in its colour, over a faint background and a
// line where a series that crosses zero has its zero.
#pragma once

#include "ui/widget.hpp"

#include <vector>

namespace rct3::ui
{

    struct PlotSeries
    {
        std::vector<float> values;
        Color colour = kWhite;
    };

    class LinePlot : public Widget
    {
    public:
        explicit LinePlot(std::string name);

        std::vector<PlotSeries> series;
        Color background = hex("#00000030");
        Color zero_colour = hex("#ffffff40");
        float dot = 2; ///< Width and height of each sample's dot

        void draw(DrawContext& context) const override;
    };

    /// Where in a plot `height` units tall a sample `value` sits, with y down, when the samples span `low`
    /// to `high`. A flat series sits in the middle.
    [[nodiscard]] float plot_y(float value, float low, float high, float height);

} // namespace rct3::ui
