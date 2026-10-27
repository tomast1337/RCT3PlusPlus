// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/pool_panel.hpp"

#include "ui/screens/footpath_panel.hpp"
#include "ui/shape.hpp"
#include "ui/skin_library.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {
        // The tabs' pictures and help, in the order of PoolPage; paths and saved pools are not built yet.
        constexpr std::array<std::string_view, 5> kTabIcons { "Pool_PoolPanel", "Pool_PathPanel", "Pool_ExtraPanel", "Pool_ChangingRoomPanel", "File_LoadGame" };
        constexpr std::array<std::string_view, 5> kTabHelp { "Pool_PoolPanel", "Pool_PathPanel", "Pool_ExtraPanel", "Pool_ChangingRoomPanel", "Pool_LoadPoolComplex" };
        constexpr std::array<bool, 5> kTabBuilt { true, false, true, true, false };
        constexpr std::array<std::string_view, 3> kModes { "Pool_PlaceWater", "Pool_PlaceTiles", "Pool_DeleteArea" };
        constexpr std::array<std::string_view, 3> kDepths { "Pool_DepthShallow", "Pool_DepthNormal", "Pool_DepthDeep" };
        constexpr std::array<std::string_view, 4> kEdges { "Pool_CurveStraight", "Pool_CurveDiagonal", "Pool_CurveSmall", "Pool_CurveLarge" };

        Point middle_of(const Rect& r)
        {
            return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
        }
    } // namespace

    PoolPanel::PoolPanel(std::string name, PoolPanelStyle style, const StringTable& strings)
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
        close_ = &add<Button>("close", std::string { }, style_.close_icon);
        close_->placement = bottom_left(pool_panel::kCloseLeft, style_.bottom - info_panel::kDiscMargin + pool_panel::kCloseBottom, info_panel::kCloseSize, info_panel::kCloseSize);
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            Button& b = add<Button>(std::string(kTabIcons[i]), style_.tab_face, std::string(kTabIcons[i]));
            b.tooltip = std::u16string(strings_.get(kTabHelp[i]));
            b.toggle = true;
            b.enabled = kTabBuilt[i];
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
            tabs_[i] = &b;
        }

        editor_page_ = &body_->add<Widget>("editor");
        const auto button = [&](std::string_view icon, std::string_view help) -> Button& {
            Button& b = editor_page_->add<Button>(std::string(icon), std::string { }, std::string(icon));
            b.tooltip = std::u16string(strings_.get(help));
            b.icon_scale = pool_panel::kButton / info_panel::kIconSize;
            return b;
        };
        for (std::size_t i = 0; i < modes_.size(); ++i)
            modes_[i] = &button(kModes[i], kModes[i]);
        for (std::size_t i = 0; i < depths_.size(); ++i)
            depths_[i] = &button(kDepths[i], kDepths[i]);
        for (std::size_t i = 0; i < edges_.size(); ++i)
        {
            edges_[i] = &button(kEdges[i], kEdges[i]);
            edges_[i]->icon_scale = pool_panel::kEdgeWidth / 37;
            // shortcut: straight edges only; the others wait for the curved pieces to be laid.
            edges_[i]->enabled = i == 0;
        }
        grid_ = &button("Land_Grid", "Pool_GridToggle");
        snap_ = &button("Pool_Height", "Pool_HeightToggle");
        height_ = &editor_page_->add<Spinner>("Pool_Height", style_.height);
        height_->minimum = pool_panel::kHeightLowest;
        height_->maximum = pool_panel::kHeightHighest;
        height_->tooltip = std::u16string(strings_.get("Pool_Height"));
        visible = false;
    }

    void PoolPanel::show(std::vector<PoolChoice> extras, std::vector<PoolChoice> rooms)
    {
        extras_ = std::move(extras);
        rooms_ = std::move(rooms);
        visible = true;
        closed_ = false;
        placing_.reset();
        changed_ = true;
        open(page_.value_or(PoolPage::Editor));
    }

    void PoolPanel::hide()
    {
        visible = false;
    }

    void PoolPanel::open(std::optional<PoolPage> page)
    {
        page_ = page;
        for (Button* b : choices_)
            (void)body_->remove(*b);
        choices_.clear();
        editor_page_->visible = page_ == PoolPage::Editor;
        if (page_ == PoolPage::Extras || page_ == PoolPage::Rooms)
            for (const PoolChoice& c : page_ == PoolPage::Extras ? extras_ : rooms_)
            {
                Button& b = body_->add<Button>(c.icon, std::string { }, c.icon);
                b.tooltip = c.help;
                b.icon_scale = (pool_panel::kCell - 4) / info_panel::kIconSize;
                b.swell = 2;
                choices_.push_back(&b);
            }
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            tabs_[i]->on = page_ == static_cast<PoolPage>(i);
        place();
    }

    Point PoolPanel::body_size() const
    {
        if (page_ == PoolPage::Editor)
            return { pool_panel::kEditorWidth, 2 * pool_panel::kButton + pool_panel::kEdgeHeight + 2 * pool_panel::kRowGap };
        if (choices_.empty())
            return { 0, 0 };
        const auto columns = (choices_.size() + pool_panel::kRows - 1) / pool_panel::kRows;
        return { pool_panel::kCell * static_cast<float>(columns), pool_panel::kCell * static_cast<float>(pool_panel::kRows) };
    }

    Rect PoolPanel::disc() const
    {
        const Rect r = rect();
        const float left = r.x0 + info_panel::kDiscMargin;
        const float bottom = r.y1 - style_.bottom;
        return { left, bottom - info_panel::kDisc, left + info_panel::kDisc, bottom };
    }

    Rect PoolPanel::strip() const
    {
        const Point body = body_size();
        const Rect d = disc();
        const float left = d.x1 - info_panel::kStripOverlap;
        const float width = std::max(pool_panel::kBodyLeft + body.x + pool_panel::kBodyRight + style_.coded.strip_shadow, info_panel::kStripNarrowest);
        return { left, d.y0, left + width, d.y0 + info_panel::kStripHeight };
    }

    void PoolPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point centre { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            const Point at = footpath_tab_offset(pool_panel::kFirstSlot + static_cast<int>(i));
            tabs_[i]->placement = top_left(centre.x + at.x - info_panel::kTabSize / 2, centre.y + at.y - info_panel::kTabSize / 2, info_panel::kTabSize, info_panel::kTabSize);
        }
        const Rect s = strip();
        const Point body = body_size();
        body_->placement = top_left(s.x0 - r.x0 + pool_panel::kBodyLeft, s.y0 - r.y0 + pool_panel::kBodyTop, body.x, body.y);
        const float status_left = info_panel::kStripOverlap + pool_panel::kStatusGap;
        status_->placement = top_left(s.x0 - r.x0 + status_left, s.y0 - r.y0, std::max(0.0f, s.width() - status_left - pool_panel::kBodyRight), style_.coded.band_height);
        status_->visible = page_.has_value();

        // The editor: depths and the surface height on top, the edges under them, the tools at the foot.
        using namespace pool_panel;
        editor_page_->placement = top_left(0, 0, kEditorWidth, body.y);
        float x = 0;
        for (Button* b : depths_)
        {
            b->placement = top_left(x, 0, kButton, kButton);
            x += kButton;
        }
        x += kGroupGap;
        snap_->placement = top_left(x, 0, kButton, kButton);
        height_->placement = top_left(x + kButton + 4, (kButton - kHeightHeight) / 2, kHeightWidth, kHeightHeight);
        for (std::size_t i = 0; i < edges_.size(); ++i)
            edges_[i]->placement = top_left(static_cast<float>(i) * (kEdgeWidth + 2), kButton + kRowGap, kEdgeWidth, kEdgeHeight);
        const float foot = kButton + kEdgeHeight + 2 * kRowGap;
        modes_[0]->placement = top_left(0, foot, kButton, kButton);
        modes_[1]->placement = top_left(kButton, foot, kButton, kButton);
        grid_->placement = top_left(2 * kButton + kGroupGap, foot, kButton, kButton);
        modes_[2]->placement = top_left(kEditorWidth - kButton, foot, kButton, kButton);

        // Down the columns, so the first two pictures stand one over the other.
        for (std::size_t i = 0; i < choices_.size(); ++i)
        {
            const auto column = static_cast<float>(i / kRows);
            const auto row = static_cast<float>(i % kRows);
            choices_[i]->placement = top_left(column * kCell + 2, row * kCell + 2, kCell - 4, kCell - 4);
        }
    }

    bool PoolPanel::covers(Point p) const
    {
        const Point m = middle_of(disc());
        const float radius = info_panel::kDisc / 2;
        const float dx = p.x - m.x;
        const float dy = p.y - m.y;
        return dx * dx + dy * dy <= radius * radius || (page_ && contains(strip(), p));
    }

    void PoolPanel::update(const UpdateContext&)
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
                const auto page = static_cast<PoolPage>(i);
                open(page_ == page ? std::nullopt : std::optional(page));
                // Opening the editor's tab puts its tool out again, as in the game.
                if (page == PoolPage::Editor && placing_)
                {
                    placing_.reset();
                    changed_ = true;
                }
                return;
            }
        const PoolEditor before = editor_;
        for (std::size_t i = 0; i < modes_.size(); ++i)
            if (modes_[i]->take_click())
            {
                editor_.mode = static_cast<PoolMode>(i);
                placing_.reset();
                changed_ = true;
            }
        for (std::size_t i = 0; i < depths_.size(); ++i)
            if (depths_[i]->take_click())
                editor_.depth = static_cast<std::int32_t>(i) + 1;
        for (Button* b : edges_)
            (void)b->take_click();
        if (grid_->take_click())
            editor_.grid = !editor_.grid;
        if (snap_->take_click())
            editor_.snap = !editor_.snap;
        height_->enabled = !editor_.snap;
        if (height_->take_change())
            editor_.height = height_->value();
        changed_ = changed_ || editor_ != before;

        status_->text.clear();
        if (page_)
            status_->text = std::u16string(strings_.get(kTabHelp[static_cast<std::size_t>(*page_)]));
        if (page_ == PoolPage::Editor)
            for (const auto& w : editor_page_->children())
                if (w->hovered())
                    status_->text = w->tooltip;
        const std::vector<PoolChoice>& shown = page_ == PoolPage::Extras ? extras_ : rooms_;
        for (std::size_t i = 0; i < choices_.size(); ++i)
        {
            if (choices_[i]->hovered())
                status_->text = shown[i].help;
            if (choices_[i]->take_click())
            {
                placing_ = std::pair { *page_, i };
                changed_ = true;
            }
        }
    }

    std::string_view PoolPanel::preview() const
    {
        if (placing_)
        {
            const std::vector<PoolChoice>& from = placing_->first == PoolPage::Extras ? extras_ : rooms_;
            return placing_->second < from.size() ? std::string_view(from[placing_->second].icon) : std::string_view { };
        }
        if (editor_.mode == PoolMode::Water)
            return kDepths[static_cast<std::size_t>(std::clamp(editor_.depth, 1, 3) - 1)];
        return kModes[std::to_underlying(editor_.mode)];
    }

    void PoolPanel::draw(DrawContext& context) const
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
        const auto lit = [&](const Button* b) { plain(b->rect(), round_corners(4), style_.chosen); };

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
            if (page_ == PoolPage::Editor)
            {
                if (!placing_)
                    lit(modes_[std::to_underlying(editor_.mode)]);
                lit(depths_[static_cast<std::size_t>(std::clamp(editor_.depth, 1, 3) - 1)]);
                lit(edges_[0]);
                if (editor_.grid)
                    lit(grid_);
                if (editor_.snap)
                    lit(snap_);
            }
            else if (placing_ && placing_->first == page_ && placing_->second < choices_.size())
                lit(choices_[placing_->second]);
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
        const Rect back { d.x0 + c.rim, d.y0 + c.rim, d.x1 - c.rim, d.y1 - c.rim };
        plain(back, round_corners(back.width() / 2), c.view_back);
        if (const std::string_view icon = preview(); !icon.empty())
        {
            const Point m = middle_of(d);
            const float h = pool_panel::kDiscIcon / 2;
            draw_skin(list, context.skins.find(icon), { m.x - h, m.y - h, m.x + h, m.y + h });
        }
    }

} // namespace rct3::ui
