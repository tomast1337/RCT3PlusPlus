// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/line_plot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rct3::ui::widgets
{

    LinePlot::LinePlot(std::string name)
        : Widget(std::move(name))
    {
    }

    float full_scale(std::span<const float> values)
    {
        float most = 0;
        for (const float v : values)
            if (std::isfinite(v))
                most = std::max(most, std::abs(v));
        if (most <= 0)
            return 1;
        const float power = std::pow(10.0f, std::floor(std::log10(most)));
        for (const float step : { 1.0f, 2.0f, 5.0f, 10.0f })
            if (most <= step * power * 1.0001f) // Slack for a value that is a round number to start with
                return step * power;
        return 10 * power;
    }

    std::array<float, 5> axis_values(float full, bool centred)
    {
        const float low = centred ? -full : 0;
        std::array<float, 5> out { };
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = full - (full - low) * static_cast<float>(i) / 4;
        return out;
    }

    float plot_y(float value, float full, bool centred, float height)
    {
        const float share = centred ? 0.5f + value / (2 * full) : value / full;
        return height * (1 - std::clamp(share, 0.0f, 1.0f));
    }

    float plot_x(float back, std::size_t span, float width)
    {
        if (span < 2)
            return width;
        return width * (1 - back / static_cast<float>(span - 1));
    }

    std::size_t LinePlot::span() const
    {
        if (slots > 0)
            return slots;
        std::size_t most = 0;
        for (const PlotSeries& s : series)
            most = std::max(most, s.values.size());
        return most;
    }

    void LinePlot::on_hover(Point p)
    {
        const Rect r = rect();
        const std::size_t across = span();
        if (r.width() <= 0 || across < 2)
            under_ = 0;
        else
            under_ = static_cast<std::size_t>(std::lround(std::clamp((r.x1 - p.x) / r.width(), 0.0f, 1.0f) * static_cast<float>(across - 1)));
    }

    bool LinePlot::on_wheel(Point notches)
    {
        if (most_slots == 0 || notches.y == 0)
            return false;
        // Chosen by eye: a fifth more or less a notch, quick across twenty years of weeks without jumping past a few.
        const float scaled = static_cast<float>(span()) * std::pow(1.25f, -notches.y);
        slots = std::clamp(static_cast<std::size_t>(std::lround(scaled)), std::min(fewest_slots, most_slots), most_slots);
        zoomed_ = slots;
        return true;
    }

    std::optional<std::size_t> LinePlot::pointed() const
    {
        if (!hovered())
            return std::nullopt;
        return under_;
    }

    void LinePlot::draw(DrawContext& context) const
    {
        const Rect r = rect();
        context.list.fill(r, background);
        for (int i = 1; i < rules; ++i)
        {
            const float y = std::round(r.y0 + r.height() * static_cast<float>(i) / static_cast<float>(rules));
            context.list.fill({ r.x0, y, r.x1, y + 1 }, rule_colour);
        }
        const std::size_t across = span();
        for (const PlotTick& t : ticks)
        {
            // Ticks before the span, zoomed in, are off the plot.
            if (t.back + 1 > static_cast<float>(across))
                continue;
            const float x = std::round(r.x0 + plot_x(t.back, across, r.width()));
            context.list.fill({ x, r.y1 - (t.major ? 2 * tick : tick), x + 1, r.y1 }, rule_colour);
        }
        if (!context.list.push_clip(r))
        {
            context.list.pop_clip();
            return;
        }
        for (const PlotSeries& s : series)
        {
            if (s.values.empty())
                continue;
            const float full = s.full > 0 ? s.full : full_scale(s.values);
            const auto at = [&](std::size_t i) {
                const float back = static_cast<float>(s.values.size() - 1 - i);
                return std::array { r.x0 + plot_x(back, across, r.width()), r.y0 + plot_y(s.values[i], full, s.centred, r.height()) };
            };
            if (s.values.size() == 1)
            {
                const auto [x, y] = at(0);
                context.list.fill({ x - line, y - line / 2, x, y + line / 2 }, s.colour);
                continue;
            }
            // Each step a band `line` wide from one sample to the next, square across its ends.
            for (std::size_t i = 0; i + 1 < s.values.size(); ++i)
            {
                const auto [x0, y0] = at(i);
                const auto [x1, y1] = at(i + 1);
                const float dx = x1 - x0;
                const float dy = y1 - y0;
                const float length = std::hypot(dx, dy);
                if (length <= 0)
                    continue;
                const float nx = -dy / length * line / 2;
                const float ny = dx / length * line / 2;
                const std::array<std::array<float, 2>, 4> band { { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } } };
                context.list.polygon(band, s.colour);
            }
        }
        if (const auto back = pointed())
        {
            const float x = std::round(r.x0 + plot_x(static_cast<float>(*back), across, r.width()));
            context.list.fill({ x, r.y0, x + 1, r.y1 }, rule_colour);
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui::widgets
