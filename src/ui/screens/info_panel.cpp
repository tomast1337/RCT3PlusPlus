// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info_panel.hpp"

#include "ui/shape.hpp"
#include "ui/widgets/label.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace rct3::ui
{

    Point info_tab_offset(int slot)
    {
        const float degrees = info_panel::kFirstAngle + info_panel::kSlotAngle * static_cast<float>(slot);
        const float a = degrees * std::numbers::pi_v<float> / 180;
        return { std::round(info_panel::kRingRadius * std::cos(a)), std::round(-info_panel::kRingRadius * std::sin(a)) };
    }

    Point info_choice_offset(int slot, std::size_t i, std::size_t count)
    {
        const float degrees = info_panel::kFirstAngle + info_panel::kSlotAngle * static_cast<float>(slot);
        float a = degrees * std::numbers::pi_v<float> / 180;
        if (count == 2)
            a += i == 0 ? info_panel::kTwoChoiceSpread : -info_panel::kTwoChoiceSpread;
        else if (count == 3)
            a += info_panel::kThreeChoiceSpread * (1 - static_cast<float>(i));
        return { std::round(info_panel::kChoiceRadius * std::cos(a)), std::round(-info_panel::kChoiceRadius * std::sin(a)) };
    }

    InfoPanel::InfoPanel(std::string name, InfoPanelStyle style)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        // The panel spans the screen so its parts can sit by its corner; it lets the pointer through elsewhere.
        placement = fill();
        body_ = &add<Widget>("body");
        body_->clip_children = true;
        close_ = &add<Button>("close", std::string { }, style_.close_icon);
        close_->placement = bottom_right(info_panel::kCloseRight, info_panel::kCloseBottom, info_panel::kCloseSize, info_panel::kCloseSize);
        visible = false;
        set_body_size(0, 0);
    }

    void InfoPanel::show(std::vector<InfoTab> tabs, std::size_t picked)
    {
        close_choices();
        close_palette();
        paints_.clear();
        for (Button* b : tabs_)
            (void)remove(*b);
        tabs_.clear();
        slots_.clear();
        tab_choices_.clear();
        actions_.clear();
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            PaintButton* paint = nullptr;
            if (!tabs[i].paints.empty())
            {
                PaintButtonStyle look = style_.paint;
                look.face = style_.tab_face;
                paint = &add<PaintButton>(std::format("tab{}", i), std::move(look), std::move(tabs[i].paints));
            }
            Button& b = paint != nullptr ? *paint : add<Button>(std::format("tab{}", i), style_.tab_face, std::move(tabs[i].icon));
            paints_.push_back(paint);
            b.tooltip = std::move(tabs[i].help);
            b.toggle = tabs[i].choices.empty() && !tabs[i].action && paint == nullptr;
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
            tabs_.push_back(&b);
            slots_.push_back(tabs[i].slot);
            tab_choices_.push_back(std::move(tabs[i].choices));
            actions_.push_back(tabs[i].action);
        }
        picked_ = picked;
        mark_picked();
        visible = true;
        closed_ = false;
        place();
    }

    void InfoPanel::hide()
    {
        close_choices();
        close_palette();
        visible = false;
    }

    void InfoPanel::open_choices(std::size_t tab)
    {
        close_choices();
        const std::vector<InfoChoice>& list = tab_choices_[tab];
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            Button& b = add<Button>(std::format("choice{}", i), std::string { }, list[i].icon);
            b.tooltip = list[i].help;
            b.enabled = list[i].enabled;
            b.icon_scale = info_panel::kChoiceIconScale;
            choices_.push_back(&b);
        }
        choosing_ = tab;
        place();
    }

    void InfoPanel::close_choices()
    {
        for (Button* b : choices_)
            (void)remove(*b);
        choices_.clear();
        choosing_.reset();
    }

    void InfoPanel::open_palette(std::size_t tab, std::size_t pot)
    {
        close_palette();
        PaintButton& pots = *paints_[tab];
        palette_ = &add<ColourPalette>("palette", style_.palette, style_.palette_count, style_.palette_columns, pots.colours.at(pot));
        pots.held = pot;
        painting_ = tab;
        painting_pot_ = pot;
        place();
    }

    void InfoPanel::close_palette()
    {
        if (palette_ != nullptr)
            (void)remove(*palette_);
        palette_ = nullptr;
        if (painting_ && *painting_ < paints_.size() && paints_[*painting_] != nullptr)
            paints_[*painting_]->held.reset();
        painting_.reset();
    }

    void InfoPanel::set_body_size(float width, float height)
    {
        body_width_ = width;
        body_height_ = height;
        place();
    }

    void InfoPanel::show_page(const InfoPage& page)
    {
        while (!body_->children().empty())
            (void)body_->remove(*body_->children().back());
        sections_.clear();
        float y = 0;
        const auto line = [&](std::string name, const std::u16string& text, float x, float width, FontRole font) {
            Label& l = body_->add<Label>(std::move(name), text, font);
            l.placement = top_left(x, y, width, info_panel::kLineHeight);
            l.style.colour = style_.text_colour;
        };
        if (!page.title.empty())
        {
            line("title", page.title, 0, info_panel::kPageWidth, FontRole::Medium);
            y += info_panel::kLineHeight + 2;
        }
        if (!page.heading.empty())
        {
            line("heading", page.heading, 0, info_panel::kPageWidth, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        if (page.sections.size() > 1)
        {
            // Each as wide as its share of the letters, so long names keep their room.
            std::size_t letters = 0;
            for (const std::u16string& s : page.sections)
                letters += s.size() + 1;
            float x = 0;
            for (std::size_t i = 0; i < page.sections.size(); ++i)
            {
                const float width = std::floor(info_panel::kPageWidth * static_cast<float>(page.sections[i].size() + 1) / static_cast<float>(letters));
                // Plain words: the shown one stands out by its colour.
                Button& b = body_->add<Button>(std::format("section{}", i), std::string { });
                b.text = page.sections[i];
                b.font = FontRole::Tiny;
                b.text_colour = i == page.section ? style_.picked_section_colour : style_.section_colour;
                b.placement = top_left(x, y, width, info_panel::kSectionHeight);
                x += width;
                sections_.push_back(&b);
            }
            y += info_panel::kSectionHeight + 2;
        }
        bool right = false; // Whether the row has a half line on its left, waiting for one on its right
        for (std::size_t i = 0; i < page.lines.size(); ++i)
        {
            const InfoLine& l = page.lines[i];
            if (l.half)
            {
                const float width = info_panel::kPageWidth / 2 - info_panel::kPairGap;
                const float x = right ? info_panel::kPageWidth - width : 0;
                line(std::format("label{}", i), l.label, x, width - info_panel::kPairValue, FontRole::Small);
                line(std::format("value{}", i), l.value, x + width - info_panel::kPairValue, info_panel::kPairValue, FontRole::Small);
                if (right)
                    y += info_panel::kLineHeight;
                right = !right;
                continue;
            }
            if (right)
            {
                y += info_panel::kLineHeight;
                right = false;
            }
            // A line without a value is text across the whole page.
            const float label_width = l.value.empty() ? info_panel::kPageWidth : info_panel::kValueColumn;
            line(std::format("label{}", i), l.label, 0, label_width, FontRole::Small);
            line(std::format("value{}", i), l.value, info_panel::kValueColumn, info_panel::kPageWidth - info_panel::kValueColumn, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        if (right)
            y += info_panel::kLineHeight;
        if (!page.note.empty())
        {
            line("note", page.note, 0, info_panel::kPageWidth, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        if (!page.graph.empty())
        {
            LinePlot& plot = body_->add<LinePlot>("graph");
            plot.placement = top_left(0, y, info_panel::kPageWidth, info_panel::kPlotHeight);
            const float width = std::floor(info_panel::kPageWidth / static_cast<float>(page.graph.size()));
            y += info_panel::kPlotHeight + 1;
            for (std::size_t i = 0; i < page.graph.size(); ++i)
            {
                const Color colour = style_.series_colours.empty() ? style_.text_colour : style_.series_colours[i % style_.series_colours.size()];
                plot.series.push_back({ page.graph[i].values, colour });
                Label& name = body_->add<Label>(std::format("series{}", i), page.graph[i].name, FontRole::Small);
                name.placement = top_left(width * static_cast<float>(i), y, width, info_panel::kLineHeight);
                name.style.colour = colour;
            }
            y += info_panel::kLineHeight;
        }
        // An empty page has no strip.
        set_body_size(y > 0 ? info_panel::kPageWidth : 0, y);
    }

    Rect InfoPanel::disc() const
    {
        const Rect r = rect();
        const float right = r.x1 - info_panel::kDiscMargin;
        const float bottom = r.y1 - info_panel::kDiscMargin;
        return { right - info_panel::kDisc, bottom - info_panel::kDisc, right, bottom };
    }

    Rect InfoPanel::view() const
    {
        const Rect d = disc();
        const float inset = (info_panel::kDisc - info_panel::kView) / 2;
        return { d.x0 + inset, d.y0 + inset, d.x1 - inset, d.y1 - inset };
    }

    Rect InfoPanel::strip() const
    {
        const float width = std::max(body_width_ + info_panel::kStripExtra, info_panel::kStripNarrowest);
        // Always full height, so it stands behind the whole disc however short its content.
        const float height = info_panel::kStripHeight;
        // Level with the disc's top, so the band along its top runs on into the disc's.
        const Rect d = disc();
        const float right = d.x0 + info_panel::kStripOverlap;
        return { right - width, d.y0, right, d.y0 + height };
    }

    void InfoPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point middle { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            const Point at = info_tab_offset(slots_[i]);
            tabs_[i]->placement = top_left(middle.x + at.x - info_panel::kTabSize / 2, middle.y + at.y - info_panel::kTabSize / 2, info_panel::kTabSize, info_panel::kTabSize);
        }
        if (choosing_)
        {
            const Point tab = info_tab_offset(slots_[*choosing_]);
            for (std::size_t i = 0; i < choices_.size(); ++i)
            {
                const Point at = info_choice_offset(slots_[*choosing_], i, choices_.size());
                choices_[i]->placement = top_left(middle.x + tab.x + at.x - info_panel::kChoiceSize / 2, middle.y + tab.y + at.y - info_panel::kChoiceSize / 2, info_panel::kChoiceSize, info_panel::kChoiceSize);
            }
        }
        if (painting_ && palette_ != nullptr)
        {
            // Above its tab, as near the tab's middle as the screen allows.
            const Point size = ColourPalette::size(style_.palette, style_.palette_count, style_.palette_columns);
            const Point tab = info_tab_offset(slots_[*painting_]);
            const float m = info_panel::kScreenMargin;
            const float x = std::clamp(middle.x + tab.x - size.x / 2, m, std::max(m, r.width() - m - size.x));
            const float y = std::clamp(middle.y + tab.y - info_panel::kTabSize / 2 - info_panel::kPaletteGap - size.y, m, std::max(m, r.height() - m - size.y));
            palette_->placement = top_left(std::round(x), std::round(y), size.x, size.y);
        }
        const Rect s = strip();
        body_->placement = top_left(s.x0 - r.x0 + info_panel::kBodyLeft, s.y0 - r.y0 + info_panel::kBodyTop, body_width_, std::min(body_height_, info_panel::kStripHeight - 2 * info_panel::kBodyTop));
    }

    void InfoPanel::mark_picked()
    {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            tabs_[i]->on = i == picked_;
    }

    bool InfoPanel::covers(Point p) const
    {
        const Rect d = disc();
        const float dx = p.x - (d.x0 + d.x1) / 2;
        const float dy = p.y - (d.y0 + d.y1) / 2;
        const float radius = info_panel::kDisc / 2;
        return dx * dx + dy * dy <= radius * radius || contains(strip(), p);
    }

    void InfoPanel::update(const UpdateContext&)
    {
        // The parts follow the screen's corner.
        place();
        if (close_->take_click())
        {
            hide();
            closed_ = true;
            return;
        }
        for (std::size_t i = 0; i < choices_.size(); ++i)
        {
            if (choices_[i]->take_click() && choosing_)
            {
                choice_now_ = { *choosing_, i };
                close_choices();
                break;
            }
        }
        if (palette_ != nullptr && painting_)
        {
            if (const auto colour = palette_->take_pick())
            {
                paint_now_ = { *painting_, painting_pot_, *colour };
                paints_[*painting_]->colours.at(painting_pot_) = *colour;
                close_palette();
            }
        }
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (paints_[i] != nullptr)
            {
                if (const auto pot = paints_[i]->take_pot())
                {
                    close_choices();
                    // Pressing it again puts the colours away.
                    if (painting_ == i)
                        close_palette();
                    else
                        open_palette(i, *pot);
                }
                continue;
            }
            if (!tabs_[i]->take_click())
                continue;
            close_palette();
            if (!tab_choices_[i].empty())
            {
                if (choosing_ == i)
                    close_choices();
                else
                    open_choices(i);
                continue;
            }
            close_choices();
            if (actions_[i])
                action_now_ = i;
            else if (i != picked_)
            {
                picked_ = i;
                picked_now_ = i;
            }
        }
        for (std::size_t i = 0; i < sections_.size(); ++i)
        {
            if (sections_[i]->take_click())
                section_now_ = i;
        }
        mark_picked();
    }

    void InfoPanel::draw(DrawContext& context) const
    {
        if (style_.look == PanelLook::Coded)
            draw_coded(context.list);
        else
            draw_skins(context);
    }

    void InfoPanel::draw_coded(DrawList& list) const
    {
        const CodedInfoPanel& c = style_.coded;
        const auto shifted = [](Rect r, float by) { return Rect { r.x0 + by, r.y0 + by, r.x1 + by, r.y1 + by }; };
        const auto shape = [](Rect r, Corners corners, Color top, Color bottom) {
            Shape out;
            out.rect = r;
            out.corners = corners;
            out.top = top;
            out.bottom = bottom;
            return out;
        };
        const auto plain = [&](Rect r, Corners corners, Color colour) { list.shape(shape(r, corners, colour, colour)); };

        if (body_width_ > 0)
        {
            const Rect s = strip();
            const Rect slab { s.x0, s.y0, s.x1 - c.strip_shadow, s.y1 - c.strip_shadow };
            const Corners top { c.corner, c.corner, 0, 0 };
            plain(shifted(slab, info_panel::kShadowOffset), top, c.shadow);
            plain(slab, top, c.body);
            plain({ slab.x0, slab.y0, slab.x1, slab.y0 + c.band_height }, top, c.band);
            list.shape(shape({ slab.x0, slab.y0 + c.band_height, slab.x1, slab.y0 + c.band_height + c.glow_height }, { }, c.band_glow, c.body));
        }

        // The disc: flat where it meets the strip, round on the right.
        const Rect d = disc();
        const float half = d.width() / 2;
        const Corners flat_left { 0, half, half, 0 };
        plain(shifted(d, info_panel::kShadowOffset), flat_left, c.shadow);
        plain(shifted(d, info_panel::kShadowOffset), round_corners(half), c.shadow);
        plain(d, flat_left, c.disc);
        plain({ d.x0, d.y0, d.x0 + half, d.y0 + c.band_height }, { }, c.band);
        Shape rim = shape(d, round_corners(half), c.disc, c.disc);
        rim.ring = c.rim;
        list.shape(rim);
        const Rect back { d.x0 + c.rim, d.y0 + c.rim, d.x1 - c.rim, d.y1 - c.rim };
        plain(back, round_corners(back.width() / 2), c.view_back);
        if (view_ != 0)
            list.disc_image(view(), view_, { 0, 0, 1, 1 });
    }

    void InfoPanel::draw_skins(DrawContext& context) const
    {
        const auto skin = [&](const std::string& name) { return name.empty() ? nullptr : context.skins.find(name); };
        if (body_width_ > 0)
            draw_skin(context.list, skin(style_.strip), strip());
        const Rect d = disc();
        const Rect shade { d.x0 + info_panel::kShadowOffset, d.y0 + info_panel::kShadowOffset, d.x1 + info_panel::kShadowOffset, d.y1 + info_panel::kShadowOffset };
        draw_skin(context.list, skin(style_.shadow), shade);
        draw_skin(context.list, skin(style_.rim_shadow), shade);
        draw_skin(context.list, skin(style_.disc), d);
        if (view_ != 0)
            context.list.disc_image(view(), view_, { 0, 0, 1, 1 });
        draw_skin(context.list, skin(style_.glass), d, { 255, 255, 255, static_cast<std::uint8_t>(std::lround(style_.glass_alpha * 255)) });
        draw_skin(context.list, skin(style_.rim), d);
    }

} // namespace rct3::ui
