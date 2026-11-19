// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build/land.hpp"

#include "core/types.hpp"
#include "ui/screens/build/paths.hpp"
#include "ui/shape.hpp"

#include <algorithm>
#include <cstdint>
#include <format>

namespace rct3::ui::screens::build
{

    namespace
    {
        Point middle_of(const Rect& r)
        {
            return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
        }
    } // namespace

    LandPanel::LandPanel(std::string name, LandPanelStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        // Spans the screen so its parts can sit by its corner; it lets the pointer through elsewhere.
        placement = fill();
        body_ = &add<Widget>("body");
        status_ = &add<widgets::Label>("status", std::u16string { }, font::FontRole::Small);
        status_->style.colour = style_.text_colour;
        status_->style.vertical = Align::Centre;
        size_ = &add<widgets::Spinner>("Land_Brush_Radius", style_.size);
        size_->minimum = 1;
        size_->tooltip = std::u16string(strings_.get("Land_Brush_RadiusHelp"));
        close_ = &add<widgets::Button>("close", std::string { }, style_.close_icon);
        close_->placement = bottom_left(land_panel::kCloseLeft, style_.bottom - info::info_panel::kDiscMargin + land_panel::kCloseBottom, info::info_panel::kCloseSize, info::info_panel::kCloseSize);
        visible = false;
    }

    void LandPanel::show(std::vector<LandTab> tabs, LandPick chosen, i32 size, i32 largest)
    {
        for (widgets::Button* b : tabs_)
            (void)remove(*b);
        tabs_.clear();
        data_ = std::move(tabs);
        for (const LandTab& t : data_)
        {
            widgets::Button& b = add<widgets::Button>(t.icon, style_.tab_face, t.icon);
            b.tooltip = t.help;
            b.toggle = true;
            b.swell = { (info::info_panel::kTabGrown - info::info_panel::kTabSize) / 2 };
            b.icon_scale = info::info_panel::kTabSize / info::info_panel::kIconSize;
            tabs_.push_back(&b);
        }
        largest_ = std::max(largest, 1);
        size_->maximum = largest_;
        size_->set_value(size);
        chosen_ = chosen;
        visible = true;
        closed_ = false;
        open(chosen.tab < data_.size() ? std::optional(chosen.tab) : std::nullopt);
    }

    void LandPanel::hide()
    {
        visible = false;
    }

    void LandPanel::open(std::optional<usize> page)
    {
        page_ = page;
        for (widgets::Button* b : tools_)
            (void)body_->remove(*b);
        tools_.clear();
        if (number_)
            (void)body_->remove(*std::exchange(number_, nullptr));
        if (page_ && !data_[*page_].number.empty())
        {
            const LandTab& t = data_[*page_];
            number_ = &body_->add<widgets::Spinner>(t.number, style_.size);
            number_->minimum = t.number_least;
            number_->maximum = t.number_most;
            number_->set_value(t.number_value);
            number_->tooltip = t.number_help;
        }
        if (page_)
            for (const LandTool& t : data_[*page_].tools)
            {
                widgets::Button& b = body_->add<widgets::Button>(t.icon, std::string { }, t.icon);
                b.tooltip = t.help;
                b.enabled = t.usable;
                b.icon_scale = (land_panel::kToolCell - 4) / land_panel::kToolIcon;
                b.swell = { 2 };
                tools_.push_back(&b);
            }
        mark();
        place();
    }

    void LandPanel::mark()
    {
        for (usize i = 0; i < tabs_.size(); ++i)
            tabs_[i]->on = page_ == i;
    }

    Point LandPanel::body_size() const
    {
        if (!page_)
            return { 0, 0 };
        const auto columns = (tools_.size() + land_panel::kRows - 1) / land_panel::kRows;
        const f32 number = number_ ? land_panel::kNumberGap + land_panel::kSizeWidth : 0;
        return { land_panel::kToolCell * static_cast<f32>(std::max<usize>(columns, 1)) + number, land_panel::kToolCell * static_cast<f32>(land_panel::kRows) };
    }

    Rect LandPanel::disc() const
    {
        const Rect r = rect();
        const f32 left = r.x0 + info::info_panel::kDiscMargin;
        const f32 bottom = r.y1 - style_.bottom;
        return { left, bottom - info::info_panel::kDisc, left + info::info_panel::kDisc, bottom };
    }

    Rect LandPanel::strip() const
    {
        const Point body = body_size();
        const Rect d = disc();
        const f32 left = d.x1 - info::info_panel::kStripOverlap;
        const f32 width = std::max(land_panel::kBodyLeft + body.x + land_panel::kBodyRight + style_.coded.strip_shadow, info::info_panel::kStripNarrowest);
        return { left, d.y0, left + width, d.y0 + info::info_panel::kStripHeight };
    }

    void LandPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point centre { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        for (usize i = 0; i < tabs_.size(); ++i)
        {
            const Point at = footpath_tab_offset(data_[i].slot);
            tabs_[i]->placement = top_left(centre.x + at.x - info::info_panel::kTabSize / 2, centre.y + at.y - info::info_panel::kTabSize / 2, info::info_panel::kTabSize, info::info_panel::kTabSize);
        }
        size_->placement = top_left(centre.x - land_panel::kSizeWidth / 2, d.y1 - r.y0 - land_panel::kSizeBottom - land_panel::kSizeHeight, land_panel::kSizeWidth, land_panel::kSizeHeight);

        const Rect s = strip();
        const Point body = body_size();
        body_->placement = top_left(s.x0 - r.x0 + land_panel::kBodyLeft, s.y0 - r.y0 + land_panel::kBodyTop, body.x, body.y);
        const f32 status_left = info::info_panel::kStripOverlap + land_panel::kStatusGap;
        status_->placement = top_left(s.x0 - r.x0 + status_left, s.y0 - r.y0, std::max(0.0f, s.width() - status_left - land_panel::kBodyRight), style_.coded.band_height);
        status_->visible = page_.has_value();
        // Down the columns, so the first two tools stand one over the other, as in the game.
        const f32 cell = land_panel::kToolCell;
        for (usize i = 0; i < tools_.size(); ++i)
        {
            const auto column = static_cast<f32>(i / land_panel::kRows);
            const auto row = static_cast<f32>(i % land_panel::kRows);
            tools_[i]->placement = top_left(column * cell + 2, row * cell + 2, cell - 4, cell - 4);
        }
        if (number_)
            number_->placement = top_left(body.x - land_panel::kSizeWidth, (body.y - land_panel::kSizeHeight) / 2, land_panel::kSizeWidth, land_panel::kSizeHeight);
    }

    bool LandPanel::covers(Point p) const
    {
        const Point m = middle_of(disc());
        const f32 radius = info::info_panel::kDisc / 2;
        const f32 dx = p.x - m.x;
        const f32 dy = p.y - m.y;
        return dx * dx + dy * dy <= radius * radius || (page_ && contains(strip(), p));
    }

    void LandPanel::update(const UpdateContext&)
    {
        // The parts follow the screen's corner.
        place();
        if (close_->take_click())
        {
            hide();
            closed_ = true;
            return;
        }
        for (usize i = 0; i < tabs_.size(); ++i)
            if (tabs_[i]->take_click())
            {
                open(page_ == i ? std::nullopt : std::optional(i));
                return;
            }
        (void)size_->take_change();
        if (number_ && page_)
        {
            (void)number_->take_change();
            data_[*page_].number_value = number_->value();
        }
        status_->text.clear();
        if (page_)
            status_->text = data_[*page_].help;
        for (usize i = 0; i < tools_.size(); ++i)
        {
            if (tools_[i]->hovered())
                status_->text = data_[*page_].tools[i].help;
            if (tools_[i]->take_click())
            {
                pick_now_ = LandPick { *page_, i };
                if (data_[*page_].setting)
                    data_[*page_].on = i;
                else
                    chosen_ = *pick_now_;
            }
        }
    }

    void LandPanel::draw(DrawContext& context) const
    {
        DrawList& list = context.list;
        const info::CodedInfoPanel& c = style_.coded;
        const auto shifted = [](Rect r, f32 by) { return Rect { r.x0 + by, r.y0 + by, r.x1 + by, r.y1 + by }; };
        const auto plain = [&](Rect r, Corners corners, Color colour) {
            Shape s;
            s.rect = r;
            s.corners = corners;
            s.top = colour;
            s.bottom = colour;
            list.shape(s);
        };

        const Rect d = disc();
        const f32 half = d.width() / 2;
        plain(shifted(d, info::info_panel::kShadowOffset), round_corners(half), c.shadow);
        if (page_)
        {
            const Rect s = strip();
            const Rect slab { s.x0, s.y0, s.x1 - c.strip_shadow, s.y1 - c.strip_shadow };
            const Corners top { c.corner, c.corner, 0, 0 };
            plain(shifted(slab, info::info_panel::kShadowOffset), top, c.shadow);
            plain(slab, top, c.body);
            plain({ slab.x0, slab.y0, slab.x1, slab.y0 + c.band_height }, top, c.band);
            const LandTab& tab = data_[*page_];
            if (const usize marked = tab.setting ? tab.on : chosen_.tool; (tab.setting || chosen_.tab == *page_) && marked < tools_.size())
                plain(tools_[marked]->rect(), round_corners((land_panel::kToolCell - 4) / 2), style_.chosen);
        }

        // The disc: round on the left, flat on the right where it meets the strip.
        plain(d, page_ ? Corners { half, 0, 0, half } : round_corners(half), c.disc);
        plain({ d.x0 + half, d.y0, d.x1, d.y0 + c.band_height }, { }, c.band);
        Shape rim;
        rim.rect = d;
        rim.corners = round_corners(half);
        rim.top = c.disc;
        rim.bottom = c.disc;
        rim.ring = c.rim;
        list.shape(rim);
        // Inside, on grey, the brush: a circle as wide as its size allows, with a dot at its middle.
        const Rect back { d.x0 + c.rim, d.y0 + c.rim, d.x1 - c.rim, d.y1 - c.rim };
        plain(back, round_corners(back.width() / 2), c.view_back);
        const Point m = middle_of(d);
        const f32 most = back.width() / 2 - 6;
        const f32 r = std::max(most * static_cast<f32>(size_->value()) / static_cast<f32>(largest_), 6.0f);
        plain({ m.x - r, m.y - r, m.x + r, m.y + r }, round_corners(r), style_.brush);
        plain({ m.x - 3, m.y - 3, m.x + 3, m.y + 3 }, round_corners(3), style_.brush_dot);
    }

} // namespace rct3::ui::screens::build
