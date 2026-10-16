// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/column_table.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    void sort_table_rows(std::vector<TableRow>& rows, std::size_t column, bool descending)
    {
        const auto key = [&](const TableRow& r) -> std::optional<double> { return column < r.keys.size() ? r.keys[column] : std::nullopt; };
        const auto text = [&](const TableRow& r) -> std::u16string_view { return column < r.cells.size() ? std::u16string_view(r.cells[column]) : std::u16string_view { }; };
        // -1 when a comes first in the lowest-first order, 1 when b does, 0 for a tie.
        const auto order = [&](const TableRow& a, const TableRow& b) {
            const auto ka = key(a);
            const auto kb = key(b);
            if (ka && kb)
                return *ka < *kb ? -1 : *kb < *ka ? 1 : 0;
            if (ka != kb)
                return ka ? -1 : 1; // Rows with a number come first either way
            const auto c = text(a).compare(text(b));
            return c < 0 ? -1 : c > 0 ? 1 : 0;
        };
        std::ranges::stable_sort(rows, [&](const TableRow& a, const TableRow& b) {
            const bool both = key(a).has_value() == key(b).has_value();
            const int o = order(a, b);
            return descending && both ? o > 0 : o < 0;
        });
    }

    ColumnTable::ColumnTable(std::string name, ColumnTableStyle style, ScrollBarStyle bar)
        : Widget(std::move(name))
        , style(std::move(style))
    {
        const float across = bar.thickness;
        bar_ = &add<ScrollBar>("bar", std::move(bar), Orientation::Vertical);
        const Rect& pad = this->style.padding;
        bar_->placement = { { 1, 0, 1, 1 }, { -across - pad.x1, pad.y0 + this->style.heading_height, -pad.x1, -pad.y1 } };
    }

    Rect ColumnTable::headings_box() const
    {
        const Rect& r = rect();
        return { r.x0 + style.padding.x0, r.y0 + style.padding.y0, r.x1 - style.padding.x1, r.y0 + style.padding.y0 + style.heading_height };
    }

    Rect ColumnTable::rows_box() const
    {
        const Rect& r = rect();
        const Rect head = headings_box();
        return { head.x0, head.y1, r.x1 - bar_->style().thickness - style.padding.x1, r.y1 - style.padding.y1 };
    }

    std::vector<std::pair<float, float>> ColumnTable::column_edges() const
    {
        const float width = rows_box().width();
        float fixed = 0;
        std::size_t shared = 0;
        for (const TableColumn& c : columns)
        {
            fixed += c.width;
            shared += c.width <= 0 ? 1 : 0;
        }
        const float share = shared > 0 ? std::max(0.0f, width - fixed) / static_cast<float>(shared) : 0;
        std::vector<std::pair<float, float>> edges;
        float x = 0;
        for (const TableColumn& c : columns)
        {
            const float w = c.width > 0 ? c.width : share;
            edges.emplace_back(x, x + w);
            x += w;
        }
        return edges;
    }

    void ColumnTable::select(std::optional<std::size_t> index)
    {
        selected_ = index && *index < rows.size() ? index : std::nullopt;
    }

    std::optional<std::size_t> ColumnTable::row_at(Point p) const
    {
        const Rect box = rows_box();
        if (!contains(box, p) || style.row_height <= 0)
            return std::nullopt;
        const auto row = static_cast<std::size_t>((p.y - box.y0 + bar_->position) / style.row_height);
        if (row >= rows.size())
            return std::nullopt;
        return row;
    }

    std::optional<std::size_t> ColumnTable::heading_at(Point p) const
    {
        const Rect box = headings_box();
        if (!contains(box, p))
            return std::nullopt;
        const auto edges = column_edges();
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            if (p.x >= box.x0 + edges[i].first && p.x < box.x0 + edges[i].second)
                return i;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> ColumnTable::take_sort()
    {
        return std::exchange(sort_now_, std::nullopt);
    }

    bool ColumnTable::take_activate()
    {
        if (activations_ == 0)
            return false;
        --activations_;
        return true;
    }

    CursorKind ColumnTable::cursor(Point p) const
    {
        // A hand over the rows, which select, and over the headings that sort.
        const auto heading = heading_at(p);
        return (heading && columns[*heading].sortable) || (!heading && row_at(p)) ? CursorKind::Hand : CursorKind::Arrow;
    }

    void ColumnTable::on_press(Point p)
    {
        if (const auto heading = heading_at(p))
        {
            if (columns[*heading].sortable)
                sort_now_ = heading;
            return;
        }
        const auto row = row_at(p);
        if (!row)
            return;
        const bool again = last_pressed_ == row && selected_ == row && since_press_ <= style.double_click;
        selected_ = row;
        if (again)
        {
            ++activations_;
            last_pressed_.reset();
        }
        else
        {
            last_pressed_ = row;
        }
        since_press_ = 0;
    }

    bool ColumnTable::on_wheel(Point notches)
    {
        return bar_->on_wheel(notches);
    }

    void ColumnTable::update(const UpdateContext& context)
    {
        since_press_ += context.elapsed;
        if (selected_ && *selected_ >= rows.size())
            selected_.reset();
        const Rect box = rows_box();
        bar_->visible = box.height();
        bar_->content = std::max(box.height(), static_cast<float>(rows.size()) * style.row_height);
        bar_->clamp();
    }

    void ColumnTable::draw(DrawContext& context) const
    {
        if (!style.background.empty())
            draw_skin(context.list, context.skins.find(style.background), rect());
        const Font* heading_font = context.fonts.get(style.heading_font);
        const Font* font = context.fonts.get(style.font);
        const auto edges = column_edges();
        const auto cell = [&](const Rect& row, std::size_t c) { return Rect { row.x0 + edges[c].first + style.cell_indent, row.y0, row.x0 + edges[c].second - style.cell_indent, row.y1 }; };

        const Rect head = headings_box();
        context.list.fill(head, style.heading_colour);
        if (context.list.push_clip(head))
        {
            for (std::size_t c = 0; c < columns.size(); ++c)
            {
                Rect box = cell(head, c);
                if (sorted_by == c)
                {
                    // A small triangle at the end of the heading, its point the way the rows run from the top.
                    const float mark = style.sort_mark;
                    const float mid = std::round((head.y0 + head.y1) / 2);
                    const float right = columns[c].align == Align::End ? box.x0 + mark : box.x1;
                    const int steps = static_cast<int>(std::ceil(mark / 2));
                    for (int step = 0; step < steps; ++step)
                    {
                        const auto i = static_cast<float>(step);
                        const float half = descending ? mark / 2 - i : i + 1;
                        const float y = mid - std::floor(mark / 4) + i;
                        context.list.fill({ right - mark / 2 - half, y, right - mark / 2 + half, y + 1 }, style.heading_text_colour);
                    }
                    if (columns[c].align == Align::End)
                        box.x0 += mark + 2;
                    else
                        box.x1 -= mark + 2;
                }
                if (heading_font != nullptr)
                    draw_text(context.list, *heading_font, columns[c].heading, box, { style.heading_text_colour, columns[c].align, Align::Centre, false });
            }
        }
        context.list.pop_clip();

        const Rect box = rows_box();
        if (context.list.push_clip(box) && style.row_height > 0)
        {
            const auto first = static_cast<std::size_t>(bar_->position / style.row_height);
            for (std::size_t i = first; i < rows.size(); ++i)
            {
                const float y = std::round(box.y0 + static_cast<float>(i) * style.row_height - bar_->position);
                if (y >= box.y1)
                    break;
                const Rect row { box.x0, y, box.x1, y + style.row_height };
                if (selected_ == i)
                {
                    if (const SkinRef* s = style.selection_skin.empty() ? nullptr : context.skins.find(style.selection_skin))
                        draw_skin(context.list, s, row);
                    else
                        context.list.fill(row, style.selection_colour);
                }
                else if (i % 2 == 1)
                {
                    context.list.fill(row, style.stripe_colour);
                }
                if (font == nullptr)
                    continue;
                for (std::size_t c = 0; c < columns.size() && c < rows[i].cells.size(); ++c)
                {
                    const Rect text = cell(row, c);
                    if (context.list.push_clip(text))
                        draw_text(context.list, *font, rows[i].cells[c], text, { style.text_colour, columns[c].align, Align::Centre, false });
                    context.list.pop_clip();
                }
            }
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui
