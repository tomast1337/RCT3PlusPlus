// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/footpath_panel.hpp"

#include "ui/shape.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace rct3::ui
{

    namespace
    {

        constexpr std::size_t kCutControl = 9;

        Point middle_of(const Rect& r)
        {
            return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
        }

    } // namespace

    Point footpath_tab_offset(int slot)
    {
        const float degrees = footpath_panel::kFirstAngle + footpath_panel::kSlotAngle * static_cast<float>(slot);
        const float a = degrees * std::numbers::pi_v<float> / 180;
        return { std::round(info_panel::kRingRadius * std::cos(a)), std::round(-info_panel::kRingRadius * std::sin(a)) };
    }

    FootpathPanel::FootpathPanel(std::string name, FootpathPanelStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        // Spans the screen so its parts can sit by its corner; it lets the pointer through elsewhere.
        placement = fill();
        body_ = &add<Widget>("body");
        const auto tab = [&](std::string tab_name, const std::string& icon, std::size_t help) -> Button& {
            Button& b = add<Button>(std::move(tab_name), style_.tab_face, icon);
            b.tooltip = std::u16string(strings_.get(style_.tab_help.at(help)));
            b.toggle = true;
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
            return b;
        };
        controls_tab_ = &tab("controls", style_.controls_icon, 0);
        follow_ = &tab("follow", style_.follow_icon, 1);
        follow_->on = true;
        route_ = &tab("route", style_.route_icon, 2);
        remove_ = &tab("remove", style_.remove_icon, 3);
        paths_tab_ = &tab("paths", style_.paths_icon, 4);
        queues_tab_ = &tab("queues", style_.queues_icon, 5);
        for (std::size_t i = 0; i < kFootpathControls; ++i)
        {
            Button& b = body_->add<Button>(style_.control_help.at(i).empty() ? std::format("control{}", i) : style_.control_help.at(i), std::string { });
            if (i >= 8)
                b.icon_scale = footpath_panel::kControlRects.at(i).height() / footpath_panel::kLargeIcon;
            controls_.at(i) = &b;
        }
        usable_.fill(true);
        text_ = &body_->add<Label>("text", std::u16string { }, FontRole::Small);
        text_->style.colour = style_.page_text_colour;
        text_->style.vertical = Align::Centre;
        style_name_ = &body_->add<Label>("style_name", std::u16string { }, FontRole::Small);
        style_name_->style.colour = style_.page_text_colour;
        status_line_ = &add<Label>("status", std::u16string { }, FontRole::Small);
        status_line_->style.colour = style_.text_colour;
        status_line_->style.vertical = Align::Centre;
        close_ = &add<Button>("close", std::string { }, style_.close_icon);
        close_->placement = bottom_left(footpath_panel::kCloseLeft, style_.bottom - info_panel::kDiscMargin + footpath_panel::kCloseBottom, info_panel::kCloseSize, info_panel::kCloseSize);
        visible = false;
        mark();
    }

    void FootpathPanel::show(std::vector<FootpathStyle> paths, std::vector<FootpathStyle> queues)
    {
        paths_ = std::move(paths);
        queues_ = std::move(queues);
        visible = true;
        closed_ = false;
        open_page(FootpathPage::Controls);
    }

    void FootpathPanel::hide()
    {
        visible = false;
    }

    void FootpathPanel::set_chosen(std::optional<FootpathPick> pick)
    {
        chosen_ = pick;
        mark();
    }

    void FootpathPanel::set_controls(const std::array<bool, kFootpathControls>& usable, std::size_t slope_control, bool cut)
    {
        usable_ = usable;
        slope_ = slope_control;
        cut_ = cut;
        mark();
    }

    void FootpathPanel::set_status(std::u16string text)
    {
        status_ = std::move(text);
    }

    Button& FootpathPanel::tab(FootpathPage page) const
    {
        switch (page)
        {
            case FootpathPage::Paths:
                return *paths_tab_;
            case FootpathPage::Queues:
                return *queues_tab_;
            default:
                return *controls_tab_;
        }
    }

    void FootpathPanel::open_page(FootpathPage page)
    {
        page_ = page;
        fill_styles();
        mark();
        place();
    }

    void FootpathPanel::fill_styles()
    {
        for (Button* b : styles_)
            (void)body_->remove(*b);
        styles_.clear();
        const bool queue = page_ == FootpathPage::Queues;
        if (page_ != FootpathPage::Paths && !queue)
            return;
        const std::vector<FootpathStyle>& list = queue ? queues_ : paths_;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            Button& b = body_->add<Button>(list[i].icon, std::string { }, list[i].icon);
            b.tooltip = list[i].name;
            b.icon_scale = (footpath_panel::kStyleCell - 4) / footpath_panel::kStyleIcon;
            b.swell = 2;
            styles_.push_back(&b);
        }
    }

    void FootpathPanel::mark()
    {
        controls_tab_->on = page_ == FootpathPage::Controls;
        paths_tab_->on = page_ == FootpathPage::Paths;
        queues_tab_->on = page_ == FootpathPage::Queues;
        const bool controls = page_ == FootpathPage::Controls;
        for (std::size_t i = 0; i < kFootpathControls; ++i)
        {
            Button& b = *controls_.at(i);
            b.visible = controls;
            b.enabled = usable_.at(i);
            if (i == kCutControl)
                b.icon = cut_ ? style_.cut_on_icon : style_.cut_off_icon;
            else
                b.icon = usable_.at(i) ? style_.usable_icons.at(i) : style_.unusable_icons.at(i);
        }
        text_->visible = controls;
        style_name_->visible = !controls;
    }

    Point FootpathPanel::body_size() const
    {
        if (page_ == FootpathPage::Controls)
            return { footpath_panel::kControlsWidth, footpath_panel::kControlsHeight };
        if (page_ == FootpathPage::None)
            return { 0, 0 };
        const auto rows = (styles_.size() + footpath_panel::kStyleColumns - 1) / footpath_panel::kStyleColumns;
        return { footpath_panel::kStyleCell * static_cast<float>(footpath_panel::kStyleColumns), footpath_panel::kStyleCell * static_cast<float>(rows) + footpath_panel::kLineHeight };
    }

    Rect FootpathPanel::disc() const
    {
        const Rect r = rect();
        const float left = r.x0 + info_panel::kDiscMargin;
        const float bottom = r.y1 - style_.bottom;
        return { left, bottom - info_panel::kDisc, left + info_panel::kDisc, bottom };
    }

    Rect FootpathPanel::strip() const
    {
        const Point body = body_size();
        const Rect d = disc();
        const float left = d.x1 - info_panel::kStripOverlap;
        const float width = std::max(footpath_panel::kBodyLeft + body.x + footpath_panel::kBodyRight + style_.coded.strip_shadow, info_panel::kStripNarrowest);
        return { left, d.y0, left + width, d.y0 + info_panel::kStripHeight };
    }

    void FootpathPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point centre { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        const auto put_tab = [&](Button& b, int slot) {
            const Point at = footpath_tab_offset(slot);
            b.placement = top_left(centre.x + at.x - info_panel::kTabSize / 2, centre.y + at.y - info_panel::kTabSize / 2, info_panel::kTabSize, info_panel::kTabSize);
        };
        put_tab(*controls_tab_, footpath_panel::kControlsSlot);
        put_tab(*follow_, footpath_panel::kFollowSlot);
        put_tab(*route_, footpath_panel::kRouteSlot);
        put_tab(*remove_, footpath_panel::kRemoveSlot);
        put_tab(*paths_tab_, footpath_panel::kPathsSlot);
        put_tab(*queues_tab_, footpath_panel::kQueuesSlot);

        const Rect s = strip();
        const Point body = body_size();
        body_->placement = top_left(s.x0 - r.x0 + footpath_panel::kBodyLeft, s.y0 - r.y0 + footpath_panel::kBodyTop, body.x, body.y);
        // Along the band from just past the disc, which covers the strip's first units.
        const float status_left = info_panel::kStripOverlap + footpath_panel::kStatusGap;
        status_line_->placement = top_left(s.x0 - r.x0 + status_left, s.y0 - r.y0, std::max(0.0f, s.width() - status_left - footpath_panel::kBodyRight), style_.coded.band_height);
        // A style page has its own line for the style's name and price, and may be narrower than the status.
        status_line_->visible = page_ == FootpathPage::Controls;
        for (std::size_t i = 0; i < kFootpathControls; ++i)
        {
            const Rect& c = footpath_panel::kControlRects.at(i);
            controls_.at(i)->placement = top_left(c.x0, c.y0, c.width(), c.height());
        }
        const Rect& t = footpath_panel::kTextRect;
        text_->placement = top_left(t.x0, t.y0, t.width(), t.height());
        const float cell = footpath_panel::kStyleCell;
        for (std::size_t i = 0; i < styles_.size(); ++i)
        {
            const auto column = static_cast<float>(i % footpath_panel::kStyleColumns);
            const auto row = static_cast<float>(i / footpath_panel::kStyleColumns);
            styles_[i]->placement = top_left(column * cell + 2, row * cell + 2, cell - 4, cell - 4);
        }
        style_name_->placement = top_left(0, body.y - footpath_panel::kLineHeight, body.x, footpath_panel::kLineHeight);
    }

    bool FootpathPanel::covers(Point p) const
    {
        const Rect d = disc();
        const Point m = middle_of(d);
        const float radius = info_panel::kDisc / 2;
        const float dx = p.x - m.x;
        const float dy = p.y - m.y;
        return dx * dx + dy * dy <= radius * radius || (page_ != FootpathPage::None && contains(strip(), p));
    }

    void FootpathPanel::update(const UpdateContext&)
    {
        // The parts follow the screen's corner.
        place();
        if (close_->take_click())
        {
            hide();
            closed_ = true;
            return;
        }
        (void)follow_->take_click();
        // Routing lays paths and removing takes them away, so turning one on turns the other off.
        if (route_->take_click() && route_->on)
            remove_->on = false;
        if (remove_->take_click() && remove_->on)
            route_->on = false;
        const std::array<std::pair<Button*, FootpathPage>, 3> pages { { { controls_tab_, FootpathPage::Controls }, { paths_tab_, FootpathPage::Paths }, { queues_tab_, FootpathPage::Queues } } };
        for (const auto& [button, page] : pages)
        {
            if (button->take_click())
                open_page(page_ == page ? FootpathPage::None : page);
        }
        for (std::size_t i = 0; i < kFootpathControls; ++i)
        {
            if (controls_.at(i)->take_click())
                control_now_ = i;
        }
        const bool queue = page_ == FootpathPage::Queues;
        const std::vector<FootpathStyle>& list = queue ? queues_ : paths_;
        std::optional<std::size_t> hovered;
        for (std::size_t i = 0; i < styles_.size(); ++i)
        {
            if (styles_[i]->hovered())
                hovered = i;
            if (styles_[i]->take_click())
            {
                chosen_ = FootpathPick { queue, i };
                style_now_ = chosen_;
                open_page(FootpathPage::Controls);
                return;
            }
        }
        // The style under the pointer, else the chosen one when it is on this page.
        std::optional<std::size_t> named = hovered;
        if (!named && chosen_ && chosen_->queue == queue)
            named = chosen_->index;
        if (named && *named < list.size())
            style_name_->text = list[*named].price.empty() ? list[*named].name : list[*named].name + u"  " + list[*named].price;
        else
            style_name_->text.clear();
        // The control under the pointer tells what it does.
        status_line_->text = status_;
        text_->text.clear();
        for (std::size_t i = 0; i < kFootpathControls; ++i)
        {
            if (controls_.at(i)->hovered())
                text_->text = std::u16string(strings_.get(style_.control_help.at(i)));
        }
    }

    void FootpathPanel::draw(DrawContext& context) const
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

        // The disc's shadow falls on the strip, so it goes down first.
        const Rect d = disc();
        const float half = d.width() / 2;
        const Corners flat_right { half, 0, 0, half };
        plain(shifted(d, info_panel::kShadowOffset), round_corners(half), c.shadow);
        if (page_ != FootpathPage::None)
        {
            const Rect s = strip();
            const Rect slab { s.x0, s.y0, s.x1 - c.strip_shadow, s.y1 - c.strip_shadow };
            const Corners top { c.corner, c.corner, 0, 0 };
            plain(shifted(slab, info_panel::kShadowOffset), top, c.shadow);
            plain(slab, top, c.body);
            plain({ slab.x0, slab.y0, slab.x1, slab.y0 + c.band_height }, top, c.band);
            // The page's paper, clear of the ring's tabs on its left and round its corner there, with a notebook's
            // margin behind a line at the open end; on the controls page a line parts the arrows from the tools below.
            const Rect paper { slab.x0 + footpath_panel::kBodyLeft - footpath_panel::kPaperLeft, slab.y0 + c.band_height + 1, slab.x1, slab.y1 - c.paper_bottom };
            plain(paper, { 0, 0, 0, c.paper_corner }, c.stripe);
            const float rule = paper.x1 - c.paper_margin - 2;
            plain({ rule + 2, paper.y0, paper.x1, paper.y1 }, { }, c.margin);
            plain({ rule, paper.y0, rule + 1, paper.y1 }, { }, c.divider);
            if (page_ == FootpathPage::Controls)
            {
                const float y = body_->rect().y0 + footpath_panel::kDividerY;
                plain({ paper.x0, y, rule, y + 1 }, { }, c.divider);
            }
            // Behind the picked slope and the chosen style.
            const auto mark_behind = [&](const Button& b) {
                const Rect r = b.rect();
                plain(r, round_corners(4), style_.chosen);
            };
            if (page_ == FootpathPage::Controls && slope_ < kFootpathControls)
                mark_behind(*controls_.at(slope_));
            const bool queue = page_ == FootpathPage::Queues;
            if (chosen_ && chosen_->queue == queue && chosen_->index < styles_.size())
                mark_behind(*styles_[chosen_->index]);
        }

        // The disc: round on the left, flat on the right where it meets the strip.
        plain(d, page_ != FootpathPage::None ? flat_right : round_corners(half), c.disc);
        plain({ d.x0 + half, d.y0, d.x1, d.y0 + c.band_height }, { }, c.band);
        Shape rim;
        rim.rect = d;
        rim.corners = round_corners(half);
        rim.top = c.disc;
        rim.bottom = c.disc;
        rim.ring = c.rim;
        list.shape(rim);
        const Rect back { d.x0 + c.rim, d.y0 + c.rim, d.x1 - c.rim, d.y1 - c.rim };
        plain(back, round_corners(back.width() / 2), c.band);
        if (chosen_)
        {
            const std::vector<FootpathStyle>& styles = chosen_->queue ? queues_ : paths_;
            if (chosen_->index < styles.size())
            {
                const Point m = middle_of(d);
                const float h = footpath_panel::kDiscIcon / 2;
                draw_skin(list, context.skins.find(styles[chosen_->index].icon), { m.x - h, m.y - h, m.x + h, m.y + h });
            }
        }
    }

} // namespace rct3::ui
