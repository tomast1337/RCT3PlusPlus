// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_picker.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using namespace build_picker;

        constexpr std::string_view kExpandAll = "RideSelectionWindow_ExpandAll";
        constexpr std::string_view kCollapseAll = "RideSelectionWindow_ContractAll";
        constexpr std::string_view kNothing = "NoItemsMeetFilterCriteria";
        constexpr std::string_view kShowGrid = "Rct3pp_BuildPicker_Grid";
        constexpr std::string_view kShowList = "Rct3pp_BuildPicker_List";
        constexpr float kSizeButtonWidth = 96;
        constexpr float kSizeButtonHeight = 22;

    } // namespace

    BuildPicker::BuildPicker(std::string name, BuildPickerStyle style, const StringTable& strings, std::u16string title, std::vector<PickerRow> rows, bool large, bool grid)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
        , title_(std::move(title))
        , data_(std::move(rows))
        , large_(large)
        , grid_(grid)
    {
        keep_inside = true;
        clip_children = true;
        close_ = &add<Button>("close", style_.title_button, style_.close_icon);
        close_->placement = top_left(kCloseAt.x, kCloseAt.y, kTitleButton, kTitleButton);
        fold_ = &add<Button>("fold", style_.title_button, style_.fold_icon);
        fold_->placement = top_left(kFoldAt.x, kFoldAt.y, kTitleButton, kTitleButton);

        list_ = &add<ScrollArea>("list", style_.scroll_bar);
        list_->placement = { { 0, 0, 1, 1 }, { kSide, kTitleHeight, -kSide, -kBottomBar } };

        view_ = &add<Button>("view", style_.text_button);
        view_->placement = bottom_left(kSide, (kBottomBar - kSizeButtonHeight) / 2, kSizeButtonWidth, kSizeButtonHeight);
        view_->text_colour = style_.button_text_colour;
        size_ = &add<Button>("size", style_.text_button);
        size_->placement = bottom_left(2 * kSide + kSizeButtonWidth, (kBottomBar - kSizeButtonHeight) / 2, kSizeButtonWidth, kSizeButtonHeight);
        size_->text_colour = style_.button_text_colour;
        label_buttons();

        add_rows(data_, nullptr);
    }

    void BuildPicker::add_rows(const std::vector<PickerRow>& rows, BuildPickerRow* heading)
    {
        for (const PickerRow& r : rows)
        {
            auto& view = list_->content().add<BuildPickerRow>("row", style_, r, heading);
            rows_.push_back(&view);
            add_rows(r.children, &view);
        }
    }

    std::optional<std::size_t> BuildPicker::take_picked()
    {
        return std::exchange(picked_out_, std::nullopt);
    }

    void BuildPicker::label_buttons()
    {
        view_->text = std::u16string(strings_.get(grid_ ? kShowList : kShowGrid));
        size_->text = std::u16string(strings_.get(large_ ? kCollapseAll : kExpandAll));
        size_->visible = !folded_ && !grid_;
    }

    bool BuildPicker::on_grip(Point p) const
    {
        const Rect& r = rect();
        return !folded_ && p.x >= r.x1 - kGrip && p.y >= r.y1 - kGrip;
    }

    CursorKind BuildPicker::cursor(Point p) const
    {
        return on_grip(p) ? CursorKind::ResizeDiagonalDown : CursorKind::Arrow;
    }

    void BuildPicker::on_press(Point p)
    {
        if (on_grip(p))
            resize_ = p;
        else if (p.y < rect().y0 + kTitleHeight)
            grab_ = p;
    }

    void BuildPicker::on_drag(Point p)
    {
        Rect& o = placement.offsets;
        if (resize_)
        {
            o.x1 = std::max(o.x0 + kMinWidth, o.x1 + p.x - resize_->x);
            o.y1 = std::max(o.y0 + kMinHeight, o.y1 + p.y - resize_->y);
            resize_ = p;
            return;
        }
        if (!grab_)
            return;
        const float dx = p.x - grab_->x;
        const float dy = p.y - grab_->y;
        o = { o.x0 + dx, o.y0 + dy, o.x1 + dx, o.y1 + dy };
        grab_ = p;
    }

    void BuildPicker::on_release(Point, bool)
    {
        grab_.reset();
        resize_.reset();
    }

    void BuildPicker::lay_out()
    {
        float y = 0;
        // The grid's columns; a heading takes a tile, and its open items follow it.
        laid_width_ = list_->content().rect().width();
        const int columns = std::max(1, static_cast<int>(laid_width_ / kTile));
        int column = 0;
        for (BuildPickerRow* row : rows_)
        {
            row->large = large_;
            row->grid = grid_;
            bool shown = true;
            float indent = 0;
            for (const BuildPickerRow* h = row->heading(); h != nullptr; h = h->heading())
            {
                shown = shown && h->open;
                indent += kIndent;
            }
            row->visible = shown;
            if (!shown)
                continue;
            if (grid_)
            {
                const float x = static_cast<float>(column) * kTile;
                row->placement = { { 0, 0, 0, 0 }, { x, y, x + kTile, y + kTile } };
                if (++column == columns)
                {
                    column = 0;
                    y += kTile;
                }
                continue;
            }
            const float height = large_ ? kLargeRow : kSmallRow;
            row->placement = { { 0, 0, 1, 0 }, { indent, y, 0, y + height } };
            y += height;
        }
        dirty_ = false;
    }

    void BuildPicker::update(const UpdateContext&)
    {
        if (close_->take_click())
            close();
        if (fold_->take_click())
        {
            folded_ = !folded_;
            if (folded_)
                unfolded_height_ = placement.offsets.y1 - placement.offsets.y0;
            list_->visible = !folded_;
            view_->visible = !folded_;
            label_buttons();
            const float height = folded_ ? kTitleHeight : unfolded_height_;
            placement.offsets.y1 = placement.offsets.y0 + height;
        }
        if (size_->take_click())
        {
            large_ = !large_;
            label_buttons();
            dirty_ = true;
        }
        if (view_->take_click())
        {
            grid_ = !grid_;
            label_buttons();
            dirty_ = true;
        }
        if (grid_ && list_->content().rect().width() != laid_width_)
            dirty_ = true;
        for (BuildPickerRow* row : rows_)
            row->dear = cash && row->row().cost > *cash;
        for (BuildPickerRow* row : rows_)
        {
            if (!row->take_click())
                continue;
            if (row->is_heading())
            {
                row->open = !row->open;
                dirty_ = true;
                continue;
            }
            for (BuildPickerRow* other : rows_)
                other->picked = other == row;
            picked_ = row->row().item;
            picked_out_ = picked_;
        }
        if (dirty_)
            lay_out();
    }

    void BuildPicker::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        draw_skin(context.list, context.skins.find(style_.background), r);
        if (const SkinRef* strip = context.skins.find(style_.button_strip))
            draw_skin(context.list, strip, { r.x0, r.y0, r.x0 + strip->size()[0], r.y0 + strip->size()[1] });
        const Font* small = context.fonts.get(FontRole::Small);
        if (small == nullptr)
            return;
        const float title_x = kFoldAt.x + kTitleButton + 2 * kSide;
        draw_text(context.list, *small, title_, { r.x0 + title_x, r.y0, r.x1 - kSide, r.y0 + kTitleHeight }, { style_.text_colour, Align::Start, Align::Centre, false });
        if (folded_)
            return;
        if (rows_.empty())
            draw_text(context.list, *small, strings_.get(kNothing), list_->rect(), { style_.text_colour, Align::Centre, Align::Centre, true });
        if (!note.empty())
        {
            const Rect bar { r.x0 + 3 * kSide + 2 * kSizeButtonWidth, r.y1 - kBottomBar, r.x1 - kSide - kGrip, r.y1 };
            draw_text(context.list, *small, note, bar, { style_.text_colour, Align::End, Align::Centre, true });
        }
        context.list.fill({ r.x1 - kGrip, r.y1 - kGrip, r.x1, r.y1 }, style_.grip_colour);
    }

    BuildPickerRow::BuildPickerRow(std::string name, const BuildPickerStyle& style, const PickerRow& row, BuildPickerRow* heading)
        : Widget(std::move(name))
        , style_(style)
        , row_(row)
        , heading_(heading)
    {
        tooltip = row_.price.empty() ? row_.name : row_.name + u" : " + row_.price;
    }

    void BuildPickerRow::on_release(Point, bool inside)
    {
        if (inside)
            clicked_ = true;
    }

    void BuildPickerRow::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        if (picked || hovered())
            context.list.fill(r, picked ? style_.picked_colour : style_.hover_colour);

        if (grid)
        {
            const float inset = (kTile - kTileIcon) / 2;
            if (const SkinRef* skin = context.skins.find(row_.icon))
                draw_skin(context.list, skin, { r.x0 + inset, r.y0 + inset, r.x0 + inset + kTileIcon, r.y0 + inset + kTileIcon });
            const std::string& corner = is_heading() ? (open ? style_.open_heading : style_.closed_heading) : row_.badge;
            if (const SkinRef* skin = context.skins.find(corner))
                draw_skin(context.list, skin, { r.x1 - kSmallBadge, r.y0, r.x1, r.y0 + kSmallBadge });
            if (dear)
                context.list.fill(r, style_.dear_colour);
            return;
        }
        if (dear)
            context.list.fill(r, style_.dear_colour);
        const float icon = large ? kLargeIcon : kSmallIcon;
        const float inset = large ? 2.0f : 1.0f;
        if (const SkinRef* skin = context.skins.find(row_.icon))
            draw_skin(context.list, skin, { r.x0 + inset, r.y0 + inset, r.x0 + inset + icon, r.y0 + inset + icon });

        const float badge = large ? kLargeBadge : kSmallBadge;
        const std::string& corner = is_heading() ? (open ? style_.open_heading : style_.closed_heading) : row_.badge;
        if (const SkinRef* skin = context.skins.find(corner))
        {
            const float size = is_heading() ? kSmallBadge : badge;
            draw_skin(context.list, skin, { r.x1 - size, r.y0, r.x1, r.y0 + size });
        }

        const float text_x = r.x0 + 2 * inset + icon + 4;
        const float right = r.x1 - (large ? 24.0f : 32.0f);
        const Font* small = context.fonts.get(FontRole::Small);
        if (small == nullptr)
            return;
        if (!context.list.push_clip(r))
        {
            context.list.pop_clip();
            return;
        }
        if (!large)
        {
            draw_text(context.list, *small, row_.name, { text_x, r.y0, right, r.y1 }, { style_.text_colour, Align::Start, Align::Centre, false });
            context.list.pop_clip();
            return;
        }
        const Font* name_font = context.fonts.get(FontRole::Medium);
        if (name_font == nullptr || break_lines(*name_font, row_.name, right - text_x).size() * name_font->line_height() > kNameBand)
            name_font = small;
        draw_text(context.list, *name_font, row_.name, { text_x, r.y0 + 2, right, r.y0 + kNameBand }, { style_.text_colour, Align::Start, Align::Centre, true });
        draw_text(context.list, *small, row_.size, { text_x, r.y0 + kNameBand, right, r.y0 + kNameBand + kLine }, { style_.text_colour, Align::Start, Align::Centre, false });
        draw_text(context.list, *small, row_.price, { text_x, r.y0 + kNameBand + kLine, right, r.y0 + kNameBand + 2 * kLine }, { style_.text_colour, Align::Start, Align::Centre, false });
        context.list.pop_clip();
    }

} // namespace rct3::ui
