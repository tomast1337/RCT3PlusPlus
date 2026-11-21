// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/line_plot.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rct3::ui::widgets
{

    LinePlot::LinePlot(std::string name)
        : Widget(std::move(name))
    {
    }

    f32 full_scale(std::span<const f32> values)
    {
        f32 most = 0;
        for (const f32 v : values)
            if (std::isfinite(v))
                most = std::max(most, std::abs(v));
        if (most <= 0)
            return 1;
        const f32 power = std::pow(10.0f, std::floor(std::log10(most)));
        for (const f32 step : { 1.0f, 2.0f, 5.0f, 10.0f })
            if (most <= step * power * 1.0001f) // Slack for a value that is a round number to start with
                return step * power;
        return 10 * power;
    }

    std::array<f32, 5> axis_values(f32 full, bool centred)
    {
        const f32 low = centred ? -full : 0;
        std::array<f32, 5> out { };
        for (usize i = 0; i < out.size(); ++i)
            out[i] = full - (full - low) * static_cast<f32>(i) / 4;
        return out;
    }

    f32 plot_y(f32 value, f32 full, bool centred, f32 height)
    {
        const f32 share = centred ? 0.5f + value / (2 * full) : value / full;
        return height * (1 - std::clamp(share, 0.0f, 1.0f));
    }

    f32 plot_x(f32 back, usize span, f32 width)
    {
        if (span < 2)
            return width;
        return width * (1 - back / static_cast<f32>(span - 1));
    }

    usize LinePlot::span() const
    {
        if (slots > 0)
            return slots;
        usize most = 0;
        for (const PlotSeries& s : series)
            most = std::max(most, s.values.size());
        return most;
    }

    void LinePlot::on_hover(Point p)
    {
        const Rect r = rect();
        const usize across = span();
        if (r.width() <= 0 || across < 2)
            under_ = 0;
        else
            under_ = static_cast<usize>(std::lround(std::clamp((r.x1 - p.x) / r.width(), 0.0f, 1.0f) * static_cast<f32>(across - 1)));
    }

    bool LinePlot::on_wheel(Point notches)
    {
        if (most_slots == 0 || notches.y == 0)
            return false;
        // Chosen by eye: a fifth more or less a notch, quick across twenty years of weeks without jumping past a few.
        const f32 scaled = static_cast<f32>(span()) * std::pow(1.25f, -notches.y);
        slots = std::clamp(static_cast<usize>(std::lround(scaled)), std::min(fewest_slots, most_slots), most_slots);
        zoomed_ = slots;
        return true;
    }

    std::optional<usize> LinePlot::pointed() const
    {
        if (!hovered())
            return std::nullopt;
        return under_;
    }

    void LinePlot::draw(DrawContext& context) const
    {
        const Rect r = rect();
        context.list.fill(r, background);
        for (i32 i = 1; i < rules; ++i)
        {
            const f32 y = std::round(r.y0 + r.height() * static_cast<f32>(i) / static_cast<f32>(rules));
            context.list.fill({ r.x0, y, r.x1, y + 1 }, rule_colour);
        }
        const usize across = span();
        for (const PlotTick& t : ticks)
        {
            // Ticks before the span, zoomed in, are off the plot.
            if (t.back + 1 > static_cast<f32>(across))
                continue;
            const f32 x = std::round(r.x0 + plot_x(t.back, across, r.width()));
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
            const f32 full = s.full > 0 ? s.full : full_scale(s.values);
            const auto at = [&](usize i) {
                const f32 back = static_cast<f32>(s.values.size() - 1 - i);
                return std::array { r.x0 + plot_x(back, across, r.width()), r.y0 + plot_y(s.values[i], full, s.centred, r.height()) };
            };
            if (s.values.size() == 1)
            {
                const auto [x, y] = at(0);
                context.list.fill({ x - line, y - line / 2, x, y + line / 2 }, s.colour);
                continue;
            }
            // Each step a band `line` wide from one sample to the next, square across its ends.
            for (usize i = 0; i + 1 < s.values.size(); ++i)
            {
                const auto [x0, y0] = at(i);
                const auto [x1, y1] = at(i + 1);
                const f32 dx = x1 - x0;
                const f32 dy = y1 - y0;
                const f32 length = std::hypot(dx, dy);
                if (length <= 0)
                    continue;
                const f32 nx = -dy / length * line / 2;
                const f32 ny = dx / length * line / 2;
                const std::array<std::array<f32, 2>, 4> band { { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } } };
                context.list.polygon(band, s.colour);
            }
        }
        if (marker && *marker + 1 <= static_cast<f32>(across))
        {
            const f32 x = std::round(r.x0 + plot_x(*marker, across, r.width()));
            context.list.fill({ x - 1, r.y0, x + 1, r.y1 }, marker_colour);
        }
        if (const auto back = pointed())
        {
            const f32 x = std::round(r.x0 + plot_x(static_cast<f32>(*back), across, r.width()));
            context.list.fill({ x, r.y0, x + 1, r.y1 }, rule_colour);
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui::widgets
