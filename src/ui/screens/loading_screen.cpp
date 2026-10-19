// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/loading_screen.hpp"

#include "strings/unicode.hpp"
#include "ui/shape.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui::loading_screen
{

    namespace
    {

        constexpr float kReference = 720; ///< The screen height the measures are for
        constexpr float kSmallest = 0.6f;
        constexpr float kLargest = 2;

        Shape plain(const Rect& r, float radius, Color top, Color bottom)
        {
            Shape s;
            s.rect = r;
            s.corners = round_corners(radius);
            s.top = top;
            s.bottom = bottom;
            return s;
        }

        void draw_line(DrawList& list, const Font* font, std::u16string_view text, const Rect& box, Color colour, Align horizontal)
        {
            if (font && !text.empty())
                draw_text(list, *font, text, box, { .colour = colour, .horizontal = horizontal, .vertical = Align::Centre });
        }

        void draw_marker(DrawList& list, const Rect& row, float size, float scale, StageState state)
        {
            const float y = (row.y0 + row.y1) / 2;
            const Rect disc { row.x0, y - size / 2, row.x0 + size, y + size / 2 };
            Shape s = plain(disc, size / 2, kEdge, kEdge);
            switch (state)
            {
                case StageState::Done:
                    break;
                case StageState::Current:
                {
                    // A ring with a dot in it.
                    Shape ring = s;
                    ring.ring = std::max(1.5f * scale, 1.0f);
                    list.shape(ring);
                    const float dot = size / 4;
                    s = plain({ disc.x0 + dot, disc.y0 + dot, disc.x1 - dot, disc.y1 - dot }, size / 4, kEdge, kEdge);
                    break;
                }
                case StageState::Waiting:
                    s.top = s.bottom = kFaint;
                    s.ring = std::max(1.0f * scale, 1.0f);
                    break;
            }
            list.shape(s);
        }

    } // namespace

    Layout layout_for(const Rect& screen, const Parts& parts)
    {
        Layout l;
        l.scale = std::clamp(screen.height() / kReference, kSmallest, kLargest);
        const float s = l.scale;
        const float width = std::max(0.0f, std::min(kColumn * s, screen.width() - 2 * kMargin * s));
        const float x0 = std::round((screen.x0 + screen.x1 - width) / 2);
        const float x1 = x0 + width;

        // The logo fits the column and its height cap without changing shape.
        float logo_w = 0;
        float logo_h = 0;
        if (parts.logo_width > 0 && parts.logo_height > 0)
        {
            const float fit = std::min(width / parts.logo_width, kLogoHeight * s / parts.logo_height);
            logo_w = std::round(parts.logo_width * fit);
            logo_h = std::round(parts.logo_height * fit);
        }

        // Heights of the parts shown, top to bottom, with a gap between each two.
        const float heading = parts.heading ? std::round(kHeadingHeight * s) : 0;
        const float bar = parts.bar ? std::round(kBarHeight * s) : 0;
        l.row = std::round(kRowHeight * s);
        const float stages = static_cast<float>(parts.stages) * l.row;
        const float detail = static_cast<float>(parts.detail_lines) * std::round(kDetailHeight * s);
        const float gap = std::round(kGap * s);
        float total = 0;
        int shown = 0;
        for (const float h : { logo_h, heading, bar, stages, detail })
        {
            if (h > 0)
            {
                total += h;
                ++shown;
            }
        }
        total += gap * static_cast<float>(std::max(shown - 1, 0));

        float y = std::round((screen.y0 + screen.y1 - total) / 2);
        const auto next = [&](float h) {
            if (h <= 0)
                return Rect { };
            const Rect r { x0, y, x1, y + h };
            y += h + gap;
            return r;
        };
        const Rect logo_row = next(logo_h);
        if (!logo_row.empty())
        {
            const float lx = std::round((x0 + x1 - logo_w) / 2);
            l.logo = { lx, logo_row.y0, lx + logo_w, logo_row.y1 };
        }
        l.heading = next(heading);
        l.bar = next(bar);
        l.stages = next(stages);
        l.detail = next(detail);

        // The split runs through the logo's middle, else the screen's.
        const float split = std::round(l.logo.empty() ? (screen.y0 + screen.y1) / 2 : (l.logo.y0 + l.logo.y1) / 2);
        l.dark = { screen.x0, screen.y0, screen.x1, split };
        l.light = { screen.x0, split, screen.x1, screen.y1 };
        if (!l.logo.empty())
            l.band = { screen.x0, l.logo.y0, screen.x1, l.logo.y1 };
        const float line_bottom = (l.band.empty() ? split : l.band.y0) - std::round(kLineAbove * s);
        l.line = { screen.x0, line_bottom - std::max(1.0f, std::round(kLineHeight * s)), screen.x1, line_bottom };
        return l;
    }

    Rect bar_fill(const Rect& bar, float progress)
    {
        const float p = std::clamp(progress, 0.0f, 1.0f);
        if (p <= 0 || bar.empty())
            return { };
        const float reach = std::max(bar.width() * p, bar.height());
        return { bar.x0, bar.y0, std::min(bar.x0 + reach, bar.x1), bar.y1 };
    }

    float stage_progress(std::size_t done, std::size_t total)
    {
        if (total == 0)
            return 1;
        return static_cast<float>(std::min(done, total)) / static_cast<float>(total);
    }

    std::u16string percent_text(float progress)
    {
        constexpr float kPercent = 100;
        return strings::to_utf16(std::format("{}%", std::lround(std::clamp(progress, 0.0f, 1.0f) * kPercent)));
    }

    void draw(DrawList& list, const Rect& screen, const Look& look, const Content& content)
    {
        const bool heading = look.heading_font && (!content.heading.empty() || content.progress);
        const std::size_t stages = look.stage_font ? content.stages.size() : 0;
        const std::size_t detail_lines = look.detail_font ? static_cast<std::size_t>(!content.detail.empty()) + static_cast<std::size_t>(!content.warning.empty()) : 0;
        const Parts parts {
            .logo_width = look.logo ? look.logo->width : 0,
            .logo_height = look.logo ? look.logo->height : 0,
            .heading = heading,
            .bar = content.progress.has_value(),
            .stages = stages,
            .detail_lines = detail_lines,
        };
        const Layout l = layout_for(screen, parts);

        list.fill(l.dark, kDark);
        list.fill(l.light, kLight);
        list.fill(l.band, kBand);
        list.fill(l.line, kLine);
        if (look.logo && !l.logo.empty())
            list.image(l.logo, look.logo->texture, { 0, 0, 1, 1 });

        if (heading)
        {
            draw_line(list, look.heading_font, content.heading, l.heading, kText, Align::Start);
            if (content.progress)
                draw_line(list, look.heading_font, percent_text(*content.progress), l.heading, kText, Align::End);
        }

        if (content.progress && !l.bar.empty())
        {
            const float round = l.bar.height() / 2;
            Shape track = plain(l.bar, round, kTrack, kTrack);
            track.outline = kEdge;
            track.outline_width = std::max(1.0f, std::floor(l.scale));
            list.shape(track);
            const Rect fill = bar_fill(l.bar, *content.progress);
            if (!fill.empty())
            {
                Shape filled = plain(fill, round, kFill, kFill);
                filled.outline = kEdge;
                filled.outline_width = track.outline_width;
                list.shape(filled);
            }
        }

        const float marker = std::round(kMarker * l.scale);
        for (std::size_t i = 0; i < stages; ++i)
        {
            const Stage& stage = content.stages[i];
            const float y = l.stages.y0 + static_cast<float>(i) * l.row;
            const Rect row { l.stages.x0, y, l.stages.x1, y + l.row };
            draw_marker(list, row, marker, l.scale, stage.state);
            const Rect text { row.x0 + marker * 2, row.y0, row.x1, row.y1 };
            draw_line(list, look.stage_font, stage.name, text, stage.state == StageState::Waiting ? kFaint : kText, Align::Start);
        }

        if (detail_lines > 0)
        {
            const float h = l.detail.height() / static_cast<float>(detail_lines);
            Rect line { l.detail.x0, l.detail.y0, l.detail.x1, l.detail.y0 + h };
            // Long details are cut at the column's edge.
            if (list.push_clip(l.detail))
            {
                if (!content.detail.empty())
                {
                    draw_line(list, look.detail_font, content.detail, line, kFaint, Align::Start);
                    line = { line.x0, line.y1, line.x1, line.y1 + h };
                }
                draw_line(list, look.detail_font, content.warning, line, kWarning, Align::Start);
            }
            list.pop_clip();
        }
    }

} // namespace rct3::ui::loading_screen
