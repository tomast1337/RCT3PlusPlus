// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/stall_menu.hpp"

#include "ui/skin_library.hpp"
#include "ui/text_layout.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        /// The pane's rows under its lines' boxes: their bands and pictures, and the presses on the handles.
        class MenuRowsBack : public Widget
        {
        public:
            explicit MenuRowsBack(StallMenu& menu)
                : Widget("rows")
                , menu_(menu)
            {
            }

            [[nodiscard]] bool takes_pointer() const override
            {
                return true;
            }
            [[nodiscard]] CursorKind cursor(Point p) const override
            {
                return menu_.cursor(p);
            }
            [[nodiscard]] CursorKind held_cursor(Point p) const override
            {
                return menu_.held_cursor(p);
            }
            void on_hover(Point p) override
            {
                menu_.on_hover(p);
                tooltip = menu_.tooltip;
            }
            void on_press(Point p) override
            {
                for (std::size_t i = 0; i < menu_.view().lines.size(); ++i)
                {
                    if (contains(menu_.handle_rect(i), p))
                    {
                        menu_.press_handle(i, p);
                        return;
                    }
                }
            }
            void on_drag(Point p) override
            {
                menu_.on_drag(p);
            }
            void on_release(Point p, bool inside) override
            {
                menu_.on_release(p, inside);
            }
            void draw(DrawContext& context) const override
            {
                menu_.draw_rows(context, rect());
            }

        private:
            StallMenu& menu_;
        };

        /// Over everything else: what is being dragged, and where it would drop.
        class MenuDragOver : public Widget
        {
        public:
            explicit MenuDragOver(const StallMenu& menu)
                : Widget("dragged")
                , menu_(menu)
            {
            }
            void draw(DrawContext& context) const override
            {
                menu_.draw_drag(context);
            }

        private:
            const StallMenu& menu_;
        };

        /// The extras on offer of `line`, by place in the view's.
        std::vector<std::size_t> offered(const MenuLine& line)
        {
            std::vector<std::size_t> out;
            for (const auto& e : line.extras)
            {
                if (e)
                    out.push_back(*e);
            }
            return out;
        }
    } // namespace

    std::vector<MenuRow> menu_rows(const StallMenuView& view)
    {
        std::vector<MenuRow> rows;
        std::optional<std::vector<std::size_t>> band;
        for (std::size_t i = 0; i < view.lines.size(); ++i)
        {
            std::vector<std::size_t> extras = offered(view.lines[i]);
            if (band != extras)
            {
                // Lines that take no extras have no ticks to head.
                if (!extras.empty())
                    rows.push_back({ std::nullopt, extras });
                band = std::move(extras);
            }
            rows.push_back({ i, { } });
        }
        return rows;
    }

    std::size_t drop_place(std::optional<std::size_t> from, std::size_t before)
    {
        return from && *from < before ? before - 1 : before;
    }

    StallMenu::StallMenu(std::string name, StallMenuStyle style, StallMenuView view)
        : Widget(std::move(name))
        , style_(std::move(style))
        , view_(std::move(view))
        , rows_(menu_rows(view_))
    {
        sync_ = &add<Tickbox>("Stall_Synchronize", style_.tick_box, style_.tick);
        sync_->ticked = view_.sync;
        sync_->tooltip = view_.sync_help;
        pane_ = &add<ScrollArea>("menu", style_.bar);
        back_ = &pane_->content().add<MenuRowsBack>(*this);
        const float bar = style_.bar.thickness;
        // Laid from the right of the rows, which stop short of the bar by the rest of the gutter.
        const float right = std::max(0.0f, style_.rows.gutter - bar);
        const float h = style_.rows.height;
        std::size_t row = 0;
        for (const MenuRow& r : rows_)
        {
            const float y = static_cast<float>(row++) * h;
            if (!r.line)
                continue;
            const std::size_t i = *r.line;
            const MenuLine& line = view_.lines[i];
            LineWidgets w;
            const float extras_right = right + style_.price_width;
            const float extras_left = extras_right + style_.extras_width;
            EditBox& name_box = pane_->content().add<EditBox>(std::format("ItemName{}", i), line.name);
            name_box.skin = style_.price.field;
            name_box.focused_skin = style_.price.field_focused;
            name_box.max_length = view_.longest_name;
            name_box.padding = { 2, 1, 2, 1 };
            name_box.placement = { { 0, 0, 1, 0 }, { style_.strip_cell, y, -extras_left, y + h } };
            name_box.tooltip = line.help;
            w.name = &name_box;
            Spinner& price = pane_->content().add<Spinner>(std::format("ItemPrice{}", i), style_.price);
            price.maximum = view_.most_price;
            price.step = view_.price_step;
            price.decimals = 2;
            price.set_value(line.price);
            price.placement = { { 1, 0, 1, 0 }, { -extras_right, y, -right, y + h } };
            price.tooltip = line.help;
            w.price = &price;
            if (!line.paints.empty())
            {
                PaintButton& pot = pane_->content().add<PaintButton>(std::format("ItemColour{}", i), style_.paint, line.paints);
                const float x = -extras_left + (style_.pot_width - h) / 2;
                pot.placement = { { 1, 0, 1, 0 }, { x, y, x + h, y + h } };
                w.pot = &pot;
            }
            // Each tick under its extra's picture in the band over the run.
            const std::vector<std::size_t> columns = offered(line);
            w.ticks.assign(line.extras.size(), nullptr);
            for (std::size_t k = 0; k < line.extras.size(); ++k)
            {
                if (!line.extras[k])
                    continue;
                const auto column = static_cast<float>(std::ranges::find(columns, *line.extras[k]) - columns.begin());
                Tickbox& tick = pane_->content().add<Tickbox>(std::format("ItemExtra{}_{}", i, k), style_.tick_box, style_.tick);
                tick.ticked = (line.ticked >> k & 1U) != 0;
                tick.tooltip = k < line.tick_help.size() ? line.tick_help[k] : std::u16string { };
                const float x = -extras_left + column * h;
                tick.placement = { { 1, 0, 1, 0 }, { x, y, x + h, y + h } };
                w.ticks[k] = &tick;
            }
            lines_.push_back(std::move(w));
        }
        back_->placement = { { 0, 0, 1, 0 }, { 0, 0, -right, static_cast<float>(rows_.size()) * h } };
        over_ = &add<MenuDragOver>(*this);
        over_->placement = fill();
        lay_out_rows();
    }

    void StallMenu::lay_out_rows()
    {
        const float cell = style_.strip_cell;
        const float strip = strip_width();
        sync_->placement = bottom_left(strip, 0, cell, cell);
        pane_->placement = { { 0, 0, 1, 1 }, { strip, 0, 0, -cell - style_.tick_gap } };
    }

    std::size_t StallMenu::strip_rows() const
    {
        // Above the bin, as many as the height takes, at least one.
        const float cell = style_.strip_cell;
        return std::max<std::size_t>(1, static_cast<std::size_t>(std::floor((rect().height() - cell) / cell)));
    }

    float StallMenu::strip_width() const
    {
        const std::size_t rows = strip_rows();
        const std::size_t columns = std::max<std::size_t>(1, (view_.goods.size() + rows - 1) / rows);
        return static_cast<float>(columns) * style_.strip_cell;
    }

    Rect StallMenu::bin_rect() const
    {
        const Rect r = rect();
        return { r.x0, r.y1 - style_.strip_cell, r.x0 + style_.strip_cell, r.y1 };
    }

    Rect StallMenu::good_rect(std::size_t i) const
    {
        const std::size_t rows = strip_rows();
        const float cell = style_.strip_cell;
        const Rect bin = bin_rect();
        const float x = bin.x0 + static_cast<float>(i / rows) * cell;
        const float y = bin.y0 - static_cast<float>(i % rows + 1) * cell;
        return { x, y, x + cell, y + cell };
    }

    Rect StallMenu::pane_rect() const
    {
        return pane_->rect();
    }

    Rect StallMenu::handle_rect(std::size_t i) const
    {
        const Rect c = back_->rect();
        const auto row = std::ranges::find(rows_, std::optional(i), &MenuRow::line);
        const float y = c.y0 + static_cast<float>(row - rows_.begin()) * style_.rows.height;
        return { c.x0, y, c.x0 + style_.strip_cell, y + style_.rows.height };
    }

    std::optional<std::size_t> StallMenu::good_at(Point p) const
    {
        for (std::size_t i = 0; i < view_.goods.size(); ++i)
        {
            if (contains(good_rect(i), p))
                return i;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> StallMenu::drop_before(Point p) const
    {
        if (!contains(pane_rect(), p))
            return std::nullopt;
        // Before the first line whose middle is below the pointer; after them all when none is.
        for (std::size_t i = 0; i < view_.lines.size(); ++i)
        {
            const Rect h = handle_rect(i);
            if (p.y < (h.y0 + h.y1) / 2)
                return i;
        }
        return view_.lines.size();
    }

    std::optional<MenuEvent> StallMenu::take_event()
    {
        return std::exchange(event_, std::nullopt);
    }

    std::vector<std::pair<std::size_t, PaintButton*>> StallMenu::pots() const
    {
        std::vector<std::pair<std::size_t, PaintButton*>> out;
        for (std::size_t i = 0; i < lines_.size(); ++i)
        {
            if (lines_[i].pot != nullptr)
                out.emplace_back(i, lines_[i].pot);
        }
        return out;
    }

    float StallMenu::scrolled() const
    {
        return pane_->vertical() ? pane_->vertical()->position : 0;
    }

    void StallMenu::scroll_to(float position)
    {
        if (auto bar = pane_->vertical())
            bar->position = position;
    }

    CursorKind StallMenu::cursor(Point p) const
    {
        if (drag_ && drag_->moving)
            return CursorKind::Grabbing;
        if (good_at(p) || contains(bin_rect(), p))
            return CursorKind::Grab;
        for (std::size_t i = 0; i < view_.lines.size(); ++i)
        {
            if (contains(handle_rect(i), p) && contains(pane_rect(), p))
                return CursorKind::Grab;
        }
        return CursorKind::Arrow;
    }

    CursorKind StallMenu::held_cursor(Point p) const
    {
        return drag_ && drag_->moving ? CursorKind::Grabbing : cursor(p);
    }

    void StallMenu::on_hover(Point p)
    {
        if (const auto good = good_at(p))
            tooltip = view_.goods[*good].help;
        else if (contains(bin_rect(), p))
            tooltip = view_.bin_help;
        else
        {
            tooltip.clear();
            for (std::size_t i = 0; i < view_.lines.size(); ++i)
            {
                const Rect h = handle_rect(i);
                if (p.y >= h.y0 && p.y < h.y1)
                    tooltip = contains(h, p) ? view_.handle_help : view_.lines[i].help;
            }
            // The extras' pictures in the bands say how much of each goes on.
            const Rect c = back_->rect();
            for (std::size_t r = 0; r < rows_.size(); ++r)
            {
                const float y = c.y0 + static_cast<float>(r) * style_.rows.height;
                if (rows_[r].line || p.y < y || p.y >= y + style_.rows.height)
                    continue;
                const float left = c.x1 - style_.price_width - style_.extras_width;
                const auto column = static_cast<std::size_t>(std::max(0.0f, std::floor((p.x - left) / style_.rows.height)));
                if (p.x >= left && column < rows_[r].extras.size())
                    tooltip = view_.extras[rows_[r].extras[column]].help;
            }
        }
    }

    void StallMenu::on_press(Point p)
    {
        if (const auto good = good_at(p))
            drag_ = Drag { .good = good, .line = std::nullopt, .from = p, .at = p };
        else if (contains(bin_rect(), p))
            bin_pressed_ = true;
    }

    void StallMenu::press_handle(std::size_t line, Point p)
    {
        drag_ = Drag { .good = std::nullopt, .line = line, .from = p, .at = p };
    }

    void StallMenu::on_drag(Point p)
    {
        if (!drag_)
            return;
        drag_->at = p;
        if (std::hypot(p.x - drag_->from.x, p.y - drag_->from.y) >= style_.drag_start)
            drag_->moving = true;
    }

    void StallMenu::on_release(Point p, bool)
    {
        if (bin_pressed_)
        {
            bin_pressed_ = false;
            if (contains(bin_rect(), p) && picked_)
                event_ = MenuEvent { MenuAction::Remove, *std::exchange(picked_, std::nullopt), 0, { } };
            return;
        }
        if (!drag_)
            return;
        if (drag_->moving)
            drop(p);
        else if (drag_->good)
            event_ = MenuEvent { MenuAction::Add, *drag_->good, static_cast<int>(view_.lines.size()), { } };
        else if (drag_->line)
            picked_ = picked_ == drag_->line ? std::nullopt : drag_->line;
        drag_.reset();
    }

    void StallMenu::drop(Point p)
    {
        if (drag_->line && contains(bin_rect(), p))
        {
            event_ = MenuEvent { MenuAction::Remove, *drag_->line, 0, { } };
            return;
        }
        const auto before = drop_before(p);
        if (!before)
            return;
        if (drag_->good)
            event_ = MenuEvent { MenuAction::Add, *drag_->good, static_cast<int>(*before), { } };
        else if (const std::size_t to = drop_place(drag_->line, *before); to != *drag_->line)
            event_ = MenuEvent { MenuAction::Move, *drag_->line, static_cast<int>(to), { } };
    }

    void StallMenu::update(const UpdateContext&)
    {
        lay_out_rows();
        if (sync_->take_change())
            event_ = MenuEvent { MenuAction::Sync, 0, sync_->ticked ? 1 : 0, { } };
        for (std::size_t i = 0; i < lines_.size(); ++i)
        {
            LineWidgets& w = lines_[i];
            if (w.price->take_change())
                event_ = MenuEvent { MenuAction::Price, i, w.price->value(), { } };
            if (w.name->take_commit())
                event_ = MenuEvent { MenuAction::Name, i, 0, w.name->text };
            for (std::size_t k = 0; k < w.ticks.size(); ++k)
            {
                if (w.ticks[k] == nullptr || !w.ticks[k]->take_change())
                    continue;
                std::uint32_t& ticked = view_.lines[i].ticked;
                ticked = w.ticks[k]->ticked ? ticked | 1U << k : ticked & ~(1U << k);
                event_ = MenuEvent { MenuAction::Extras, i, static_cast<int>(ticked), { } };
            }
        }
    }

    void StallMenu::draw(DrawContext& context) const
    {
        draw_skin(context.list, context.skins.find(style_.bin), bin_rect(), bin_pressed_ ? style_.picked : kWhite);
        for (std::size_t i = 0; i < view_.goods.size(); ++i)
            draw_skin(context.list, context.skins.find(view_.goods[i].icon), good_rect(i));
        // The tick's words, beside it.
        if (const auto f = context.fonts.get(FontRole::Small))
        {
            const Rect tick = sync_->rect();
            draw_text(context.list, *f, view_.sync_help, { tick.x1 + 4, tick.y0, rect().x1, tick.y1 }, { style_.help_colour, Align::Start, Align::Centre, false });
        }
    }

    void StallMenu::draw_rows(DrawContext& context, const Rect& content) const
    {
        const float h = style_.rows.height;
        const float left = content.x1 - style_.price_width - style_.extras_width;
        for (std::size_t r = 0; r < rows_.size(); ++r)
        {
            const Rect row { content.x0, content.y0 + static_cast<float>(r) * h, content.x1, content.y0 + static_cast<float>(r + 1) * h };
            if (!rows_[r].line)
            {
                context.list.fill(row, style_.rows.band);
                for (std::size_t k = 0; k < rows_[r].extras.size(); ++k)
                {
                    const float x = left + static_cast<float>(k) * h;
                    draw_skin(context.list, context.skins.find(view_.extras[rows_[r].extras[k]].icon), { x, row.y0, x + h, row.y1 });
                }
                continue;
            }
            const std::size_t i = *rows_[r].line;
            if (picked_ == i)
                context.list.fill(row, style_.picked);
            else
                fill_row(context.list, style_.rows, row, i);
            draw_skin(context.list, context.skins.find(view_.lines[i].icon), { row.x0, row.y0, row.x0 + h, row.y1 }, drag_ && drag_->moving && drag_->line == i ? Color { 255, 255, 255, 96 } : kWhite);
        }
    }

    void StallMenu::draw_drag(DrawContext& context) const
    {
        if (!drag_ || !drag_->moving)
            return;
        const std::string& icon = drag_->good ? view_.goods[*drag_->good].icon : view_.lines[*drag_->line].icon;
        const float h = style_.rows.height;
        if (const auto before = drop_before(drag_->at))
        {
            // A line across the pane where it would go.
            const Rect pane = pane_rect();
            const float y = *before < view_.lines.size() ? handle_rect(*before).y0 : (view_.lines.empty() ? back_->rect().y0 + h : handle_rect(view_.lines.size() - 1).y1);
            if (context.list.push_clip(pane))
                context.list.fill({ pane.x0, y - 1, back_->rect().x1, y + 1 }, style_.drop);
            context.list.pop_clip();
        }
        draw_skin(context.list, context.skins.find(icon), { drag_->at.x - h / 2, drag_->at.y - h / 2, drag_->at.x + h / 2, drag_->at.y + h / 2 });
    }

} // namespace rct3::ui
