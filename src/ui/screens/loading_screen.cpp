// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/loading_screen.hpp"

#include "strings/unicode.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace rct3::ui::loading_screen
{

    namespace
    {

        constexpr float kPercent = 100;

        /// A rectangle `width` by `height` centred on the point.
        Rect centred(float x, float y, float width, float height)
        {
            return { x - width / 2, y - height / 2, x + width / 2, y + height / 2 };
        }

        /// The bar: its ends at their own size, scaled with the band; between them a pale track filled from
        /// the left as far as the progress, edged above and below and where the fill stops.
        void draw_bar(DrawList& list, const Layout& layout, const Look& look, float progress)
        {
            Rect bar = layout.bar;
            const auto end_width = [&](const SkinRef* end) { return end ? end->size()[0] * layout.scale : 0.0f; };
            if (look.bar_left)
                bar.y1 = bar.y0 + look.bar_left->size()[1] * layout.scale;
            const float left = end_width(look.bar_left);
            const float right = end_width(look.bar_right);
            draw_skin(list, look.bar_left, { bar.x0, bar.y0, bar.x0 + left, bar.y1 });
            draw_skin(list, look.bar_right, { bar.x1 - right, bar.y0, bar.x1, bar.y1 });

            const Rect track { bar.x0 + left, bar.y0, bar.x1 - right, bar.y1 };
            if (track.empty())
                return;
            const float edge = std::max(1.0f, std::floor(layout.scale));
            list.fill(track, kBarBack);
            const float reach = track.x0 + track.width() * std::clamp(progress, 0.0f, 1.0f);
            list.fill({ track.x0, track.y0, reach, track.y1 }, kBarFill);
            list.fill({ track.x0, track.y0, track.x1, track.y0 + edge }, kBarEdge);
            list.fill({ track.x0, track.y1 - edge, track.x1, track.y1 }, kBarEdge);
            if (reach > track.x0)
                list.fill({ std::max(track.x0, reach - edge), track.y0, reach, track.y1 }, kBarEdge);
        }

        void draw_line(DrawList& list, const Font* font, std::u16string_view text, const Rect& box, Align vertical)
        {
            if (font && !text.empty())
                draw_text(list, *font, text, box, { .colour = kText, .horizontal = Align::Start, .vertical = vertical });
        }

    } // namespace

    Layout layout_for(const Rect& screen, float band_width, float band_height)
    {
        Layout l;
        const float cx = (screen.x0 + screen.x1) / 2;
        const float cy = (screen.y0 + screen.y1) / 2;
        if (band_width > 0 && band_height > 0)
            l.scale = std::min(screen.width() * kWidthShare / band_width, screen.height() * kHeightShare / band_height);
        else
            l.scale = 0;
        // Whole pixels, so the point-sampled band does not shimmer along its edges.
        const float w = std::round(band_width * l.scale);
        const float h = std::round(band_height * l.scale);
        const Rect band = centred(cx, cy, w, h);
        l.band = { std::round(band.x0), std::round(band.y0), std::round(band.x0) + w, std::round(band.y0) + h };

        const float split = std::round(l.band.y0 + kSplit * l.scale);
        l.dark = { screen.x0, screen.y0, screen.x1, split };
        l.light = { screen.x0, split, screen.x1, screen.y1 };
        l.line = { screen.x0, std::round(l.band.y0 + kLineTop * l.scale), screen.x1, std::round(l.band.y0 + kLineBottom * l.scale) };
        l.heading = l.on_band(kHeading);
        l.step = l.on_band(kStep);
        l.bar = l.on_band(kBar);
        return l;
    }

    std::string_view band_for(std::string_view language)
    {
        if (language == "Chinese")
            return kBandTraditional;
        if (language == "SChinese")
            return kBandSimplified;
        return kBand;
    }

    float stage_progress(std::size_t done, std::size_t total)
    {
        if (total == 0)
            return 1;
        return static_cast<float>(std::min(done, total)) / static_cast<float>(total);
    }

    std::u16string step_line(std::u16string_view stage, float progress)
    {
        const long percent = std::lround(std::clamp(progress, 0.0f, 1.0f) * kPercent);
        std::u16string line(stage);
        line += strings::to_utf16(std::format(" ({}%)", percent));
        return line;
    }

    void draw(DrawList& list, const Rect& screen, const Look& look, const Content& content)
    {
        const auto band_size = look.band ? look.band->size() : std::array<float, 2> { };
        const Layout layout = layout_for(screen, band_size[0], band_size[1]);
        if (!look.band)
        {
            list.fill(screen, kDark);
            return;
        }
        list.fill(layout.dark, kDark);
        list.fill(layout.light, kLight);
        list.fill(layout.line, kLine);
        draw_skin(list, look.band, layout.band);
        // Text stays inside the band, however small the band is drawn.
        if (list.push_clip(layout.band))
        {
            draw_line(list, look.heading_font, content.heading, layout.heading, Align::End);
            draw_line(list, look.step_font, content.step, layout.step, Align::Centre);
        }
        list.pop_clip();
        if (content.progress)
            draw_bar(list, layout, look, *content.progress);
    }

} // namespace rct3::ui::loading_screen
