// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/line_plot.hpp"

#include <algorithm>

namespace rct3::ui
{

    LinePlot::LinePlot(std::string name)
        : Widget(std::move(name))
    {
    }

    float plot_y(float value, float low, float high, float height)
    {
        if (high <= low)
            return height / 2;
        return height * (high - value) / (high - low);
    }

    void LinePlot::draw(DrawContext& context) const
    {
        const Rect r = rect();
        context.list.fill(r, background);
        const float inner = r.height() - dot;
        for (const PlotSeries& s : series)
        {
            if (s.values.empty())
                continue;
            const auto [low, high] = std::ranges::minmax(s.values);
            if (low < 0 && high > 0)
            {
                const float y = r.y0 + plot_y(0, low, high, inner);
                context.list.fill({ r.x0, y, r.x1, y + 1 }, zero_colour);
            }
            const float step = s.values.size() > 1 ? (r.width() - dot) / static_cast<float>(s.values.size() - 1) : 0;
            for (std::size_t i = 0; i < s.values.size(); ++i)
            {
                const float x = r.x0 + step * static_cast<float>(i);
                const float y = r.y0 + plot_y(s.values[i], low, high, inner);
                context.list.fill({ x, y, x + dot, y + dot }, s.colour);
            }
        }
    }

} // namespace rct3::ui
