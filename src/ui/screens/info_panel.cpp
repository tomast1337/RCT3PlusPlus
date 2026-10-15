// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info_panel.hpp"

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
        for (Button* b : tabs_)
            (void)remove(*b);
        tabs_.clear();
        slots_.clear();
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            Button& b = add<Button>(std::format("tab{}", i), style_.tab_face, std::move(tabs[i].icon));
            b.tooltip = std::move(tabs[i].help);
            b.toggle = true;
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
            tabs_.push_back(&b);
            slots_.push_back(tabs[i].slot);
        }
        picked_ = tabs_.empty() ? 0 : std::min(picked, tabs_.size() - 1);
        mark_picked();
        visible = true;
        closed_ = false;
        place();
    }

    void InfoPanel::hide()
    {
        visible = false;
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
        float y = 0;
        const auto line = [&](std::string name, const std::u16string& text, float x, float width, FontRole font) {
            Label& l = body_->add<Label>(std::move(name), text, font);
            l.placement = top_left(x, y, width, info_panel::kLineHeight);
        };
        if (!page.title.empty())
        {
            line("title", page.title, 0, info_panel::kPageWidth, FontRole::Medium);
            y += info_panel::kLineHeight + 2;
        }
        for (std::size_t i = 0; i < page.lines.size(); ++i)
        {
            line(std::format("label{}", i), page.lines[i].label, 0, info_panel::kValueColumn, FontRole::Small);
            line(std::format("value{}", i), page.lines[i].value, info_panel::kValueColumn, info_panel::kPageWidth - info_panel::kValueColumn, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        if (!page.note.empty())
        {
            line("note", page.note, 0, info_panel::kPageWidth, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        set_body_size(info_panel::kPageWidth, y);
    }

    Rect InfoPanel::disc() const
    {
        const Rect r = rect();
        const float right = r.x1 - info_panel::kDiscMargin;
        const float bottom = r.y1 - info_panel::kDiscMargin;
        return { right - info_panel::kDisc, bottom - info_panel::kDisc, right, bottom };
    }

    Rect InfoPanel::strip() const
    {
        const Rect r = rect();
        const float width = std::max(body_width_ + info_panel::kStripExtra, info_panel::kStripNarrowest);
        const float height = std::min(body_height_ + 2 * info_panel::kBodyTop, info_panel::kStripHeight);
        const float right = disc().x0 + info_panel::kStripOverlap;
        const float bottom = r.y1 - info_panel::kStripBottom;
        return { right - width, bottom - height, right, bottom };
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
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (tabs_[i]->take_click() && i != picked_)
            {
                picked_ = i;
                picked_now_ = i;
            }
        }
        mark_picked();
    }

    void InfoPanel::draw(DrawContext& context) const
    {
        const auto skin = [&](const std::string& name) { return name.empty() ? nullptr : context.skins.find(name); };
        if (body_width_ > 0)
            draw_skin(context.list, skin(style_.strip), strip());
        const Rect d = disc();
        const Rect shade { d.x0 + info_panel::kShadowOffset, d.y0 + info_panel::kShadowOffset, d.x1 + info_panel::kShadowOffset, d.y1 + info_panel::kShadowOffset };
        draw_skin(context.list, skin(style_.shadow), shade);
        draw_skin(context.list, skin(style_.rim_shadow), shade);
        draw_skin(context.list, skin(style_.disc), d);
        draw_skin(context.list, skin(style_.glass), d, { 255, 255, 255, static_cast<std::uint8_t>(std::lround(style_.glass_alpha * 255)) });
        draw_skin(context.list, skin(style_.rim), d);
    }

} // namespace rct3::ui
