// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/land_panel.hpp"

#include "ui/screens/footpath_panel.hpp"
#include "ui/shape.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
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
        status_ = &add<Label>("status", std::u16string { }, FontRole::Small);
        status_->style.colour = style_.text_colour;
        status_->style.vertical = Align::Centre;
        size_ = &add<Spinner>("Land_Brush_Radius", style_.size);
        size_->minimum = 1;
        size_->tooltip = std::u16string(strings_.get("Land_Brush_RadiusHelp"));
        close_ = &add<Button>("close", std::string { }, style_.close_icon);
        close_->placement = bottom_left(land_panel::kCloseLeft, style_.bottom - info_panel::kDiscMargin + land_panel::kCloseBottom, info_panel::kCloseSize, info_panel::kCloseSize);
        visible = false;
    }

    void LandPanel::show(std::vector<LandTab> tabs, LandPick chosen, int size, int largest)
    {
        for (Button* b : tabs_)
            (void)remove(*b);
        tabs_.clear();
        data_ = std::move(tabs);
        for (const LandTab& t : data_)
        {
            Button& b = add<Button>(t.icon, style_.tab_face, t.icon);
            b.tooltip = t.help;
            b.toggle = true;
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
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

    void LandPanel::open(std::optional<std::size_t> page)
    {
        page_ = page;
        for (Button* b : tools_)
            (void)body_->remove(*b);
        tools_.clear();
        if (page_)
            for (const LandTool& t : data_[*page_].tools)
            {
                Button& b = body_->add<Button>(t.icon, std::string { }, t.icon);
                b.tooltip = t.help;
                b.enabled = t.usable;
                b.icon_scale = (land_panel::kToolCell - 4) / land_panel::kToolIcon;
                b.swell = 2;
                tools_.push_back(&b);
            }
        mark();
        place();
    }

    void LandPanel::mark()
    {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            tabs_[i]->on = page_ == i;
    }

    Point LandPanel::body_size() const
    {
        if (!page_)
            return { 0, 0 };
        const auto columns = (tools_.size() + land_panel::kRows - 1) / land_panel::kRows;
        return { land_panel::kToolCell * static_cast<float>(std::max<std::size_t>(columns, 1)), land_panel::kToolCell * static_cast<float>(land_panel::kRows) };
    }

    Rect LandPanel::disc() const
    {
        const Rect r = rect();
        const float left = r.x0 + info_panel::kDiscMargin;
        const float bottom = r.y1 - style_.bottom;
        return { left, bottom - info_panel::kDisc, left + info_panel::kDisc, bottom };
    }

    Rect LandPanel::strip() const
    {
        const Point body = body_size();
        const Rect d = disc();
        const float left = d.x1 - info_panel::kStripOverlap;
        const float width = std::max(land_panel::kBodyLeft + body.x + land_panel::kBodyRight + style_.coded.strip_shadow, info_panel::kStripNarrowest);
        return { left, d.y0, left + width, d.y0 + info_panel::kStripHeight };
    }

    void LandPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point centre { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            const Point at = footpath_tab_offset(data_[i].slot);
            tabs_[i]->placement = top_left(centre.x + at.x - info_panel::kTabSize / 2, centre.y + at.y - info_panel::kTabSize / 2, info_panel::kTabSize, info_panel::kTabSize);
        }
        size_->placement = top_left(centre.x - land_panel::kSizeWidth / 2, d.y1 - r.y0 - land_panel::kSizeBottom - land_panel::kSizeHeight, land_panel::kSizeWidth, land_panel::kSizeHeight);

        const Rect s = strip();
        const Point body = body_size();
        body_->placement = top_left(s.x0 - r.x0 + land_panel::kBodyLeft, s.y0 - r.y0 + land_panel::kBodyTop, body.x, body.y);
        const float status_left = info_panel::kStripOverlap + land_panel::kStatusGap;
        status_->placement = top_left(s.x0 - r.x0 + status_left, s.y0 - r.y0, std::max(0.0f, s.width() - status_left - land_panel::kBodyRight), style_.coded.band_height);
        status_->visible = page_.has_value();
        // Down the columns, so the first two tools stand one over the other, as in the game.
        const float cell = land_panel::kToolCell;
        for (std::size_t i = 0; i < tools_.size(); ++i)
        {
            const auto column = static_cast<float>(i / land_panel::kRows);
            const auto row = static_cast<float>(i % land_panel::kRows);
            tools_[i]->placement = top_left(column * cell + 2, row * cell + 2, cell - 4, cell - 4);
        }
    }

    bool LandPanel::covers(Point p) const
    {
        const Point m = middle_of(disc());
        const float radius = info_panel::kDisc / 2;
        const float dx = p.x - m.x;
        const float dy = p.y - m.y;
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
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i]->take_click())
            {
                open(page_ == i ? std::nullopt : std::optional(i));
                return;
            }
        (void)size_->take_change();
        status_->text.clear();
        if (page_)
            status_->text = data_[*page_].help;
        for (std::size_t i = 0; i < tools_.size(); ++i)
        {
            if (tools_[i]->hovered())
                status_->text = data_[*page_].tools[i].help;
            if (tools_[i]->take_click())
            {
                chosen_ = { *page_, i };
                pick_now_ = chosen_;
            }
        }
    }

    void LandPanel::draw(DrawContext& context) const
    {
        DrawList& list = context.list;
        const CodedInfoPanel& c = style_.coded;
        const auto shifted = [](Rect r, float by) { return Rect { r.x0 + by, r.y0 + by, r.x1 + by, r.y1 + by }; };
        const auto plain = [&](Rect r, Corners corners, Color colour) {
            Shape s;
            s.rect = r;
            s.corners = corners;
            s.top = colour;
            s.bottom = colour;
            list.shape(s);
        };

        const Rect d = disc();
        const float half = d.width() / 2;
        plain(shifted(d, info_panel::kShadowOffset), round_corners(half), c.shadow);
        if (page_)
        {
            const Rect s = strip();
            const Rect slab { s.x0, s.y0, s.x1 - c.strip_shadow, s.y1 - c.strip_shadow };
            const Corners top { c.corner, c.corner, 0, 0 };
            plain(shifted(slab, info_panel::kShadowOffset), top, c.shadow);
            plain(slab, top, c.body);
            plain({ slab.x0, slab.y0, slab.x1, slab.y0 + c.band_height }, top, c.band);
            if (chosen_.tab == *page_ && chosen_.tool < tools_.size())
                plain(tools_[chosen_.tool]->rect(), round_corners((land_panel::kToolCell - 4) / 2), style_.chosen);
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
        const float most = back.width() / 2 - 6;
        const float r = std::max(most * static_cast<float>(size_->value()) / static_cast<float>(largest_), 6.0f);
        plain({ m.x - r, m.y - r, m.x + r, m.y + r }, round_corners(r), style_.brush);
        plain({ m.x - 3, m.y - 3, m.x + 3, m.y + 3 }, round_corners(3), style_.brush_dot);
    }

} // namespace rct3::ui
