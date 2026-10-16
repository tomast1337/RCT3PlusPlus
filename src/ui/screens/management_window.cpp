// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {
        /// A text button's width for `text`, without measuring: enough for the small font's letters.
        constexpr float kLetterWidth = 6;
        constexpr float kTextButtonMargin = 24;
        constexpr float kNarrowestTextButton = 70;
        constexpr std::size_t kSeriesPerRow = 4; ///< Names of a graph's series in a row under it, at most

        Placement band(float x0, float x1, float y, float height)
        {
            // From fraction x0 to fraction x1 of the page's width.
            return { { x0, 0, x1, 0 }, { 0, y, 0, y + height } };
        }
    } // namespace

    ManagementWindow::ManagementWindow(std::string name, const ManagementWindowStyle& style, float width, float height, std::vector<ManagementTab> tabs, std::size_t picked)
        : Window(std::move(name), style.window, width, height)
        , style_(style)
    {
        Widget& body = content();
        caption_ = &body.add<Label>("caption", std::u16string { }, style.caption_font);
        caption_->style.vertical = Align::Centre;
        caption_->style.colour = style.caption_colour;
        const float tabs_width = tabs.empty() ? 0 : style.tab_step * static_cast<float>(tabs.size() - 1) + style.tab_size;
        caption_->placement = { { 0, 0, 1, 0 }, { style.caption_x, 0, -tabs_width - style.gap, style.caption_height } };
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            auto& tab = body.add<Button>(std::format("tab{}", i), std::move(tabs[i].icon));
            tab.toggle = true;
            tab.tooltip = tabs[i].name;
            const float right = tabs_width - style.tab_step * static_cast<float>(i) - style.tab_size;
            tab.placement = top_right(right, 0, style.tab_size, style.tab_size);
            tabs_.push_back(&tab);
            names_.push_back(std::move(tabs[i].name));
        }
        // With one tab there is nothing to pick.
        if (tabs_.size() == 1)
            tabs_.front()->visible = false;
        sorts_.resize(tabs_.size());

        page_ = &body.add<Widget>("page");
        page_->placement = { { 0, 0, 1, 1 }, { 0, tabs_.empty() ? 0 : style.tab_bar_height, 0, 0 } };
        lines_ = &page_->add<Widget>("lines");
        table_ = &page_->add<ColumnTable>("table", style.table, style.scroll);
        table_->visible = false;
        open_tab(std::min(picked, tabs_.empty() ? 0 : tabs_.size() - 1));
    }

    void ManagementWindow::open_tab(std::size_t i)
    {
        if (i >= tabs_.size())
        {
            caption_->text.clear();
            return;
        }
        tab_ = i;
        for (std::size_t t = 0; t < tabs_.size(); ++t)
            tabs_[t]->on = t == i;
        caption_->text = names_[i];
    }

    void ManagementWindow::show_page(const ManagementPage& shown)
    {
        page_age_ = 0;
        while (!lines_->children().empty())
            (void)lines_->remove(*lines_->children().back());
        buttons_.clear();

        float y = 0;
        const auto label = [&](std::string name, const std::u16string& text, Placement placement, Color colour, Align align = Align::Start) {
            Label& l = lines_->add<Label>(std::move(name), text, FontRole::Small);
            l.placement = placement;
            l.style.colour = colour;
            l.style.horizontal = align;
        };
        bool right = false; // Whether the row has a half line on its left, waiting for one on its right
        for (std::size_t i = 0; i < shown.lines.size(); ++i)
        {
            const InfoLine& l = shown.lines[i];
            if (l.half)
            {
                const float x0 = right ? 0.5f : 0;
                label(std::format("label{}", i), l.label, { { x0, 0, x0 + 0.5f, 0 }, { right ? style_.pair_gap / 2 : 0, y, -style_.pair_gap / 2 - 60, y + style_.line_height } }, style_.text_colour);
                label(std::format("value{}", i), l.value, { { x0 + 0.5f, 0, x0 + 0.5f, 0 }, { -style_.pair_gap / 2 - 60, y, -style_.pair_gap / 2, y + style_.line_height } }, style_.text_colour, Align::End);
                if (right)
                    y += style_.line_height;
                right = !right;
                continue;
            }
            if (right)
            {
                y += style_.line_height;
                right = false;
            }
            if (l.value.empty())
            {
                // A heading over the lines below it, with a little room above unless it starts the page.
                if (y > 0)
                    y += style_.gap / 2;
                label(std::format("label{}", i), l.label, band(0, 1, y, style_.line_height), style_.heading_colour);
            }
            else
            {
                label(std::format("label{}", i), l.label, band(0, style_.value_column, y, style_.line_height), style_.text_colour);
                label(std::format("value{}", i), l.value, band(style_.value_column, 1, y, style_.line_height), style_.text_colour);
            }
            y += style_.line_height;
        }
        if (right)
            y += style_.line_height;
        // Room the buttons take from the page's bottom.
        float bottom = 0;
        for (const ManagementButton& b : shown.buttons)
            bottom = std::max(bottom, (b.text.empty() ? style_.icon_button_size : style_.button_height) + style_.gap);
        if (!shown.graph.empty())
        {
            if (y > 0)
                y += style_.gap;
            // The series' names under the plot, a few to a row. A page with nothing under its graph gives the
            // graph all the room down to the buttons.
            const std::size_t n = shown.graph.size();
            const std::size_t per_row = n <= kSeriesPerRow ? n : (n + 1) / 2;
            const std::size_t name_rows = (n + per_row - 1) / per_row;
            const float names = static_cast<float>(name_rows) * style_.line_height;
            const bool fill = shown.columns.empty() && shown.note.empty();
            LinePlot& plot = lines_->add<LinePlot>("graph");
            plot.placement = fill ? Placement { { 0, 0, 1, 1 }, { 0, y, 0, -bottom - names - 1 } } : band(0, 1, y, style_.plot_height);
            plot.background = style_.plot_background;
            plot.zero_colour = style_.plot_zero;
            for (std::size_t i = 0; i < n; ++i)
            {
                const Color colour = style_.series_colours.empty() ? style_.text_colour : style_.series_colours[i % style_.series_colours.size()];
                plot.series.push_back({ shown.graph[i].values, colour });
                const float column = static_cast<float>(i % per_row);
                const float row = static_cast<float>(i / per_row);
                const float across = static_cast<float>(per_row);
                Placement where = band(column / across, (column + 1) / across, y + style_.plot_height + 1 + row * style_.line_height, style_.line_height);
                if (fill)
                    where = { { column / across, 1, (column + 1) / across, 1 }, { 0, -bottom - names + row * style_.line_height, 0, -bottom - names + (row + 1) * style_.line_height } };
                label(std::format("series{}", i), shown.graph[i].name, where, colour);
            }
            y += style_.plot_height + 1 + names;
        }
        if (!shown.note.empty())
        {
            if (y > 0)
                y += style_.gap / 2;
            Label& note = lines_->add<Label>("note", shown.note, FontRole::Small);
            note.placement = band(0, 1, y, style_.line_height);
            note.style.colour = style_.text_colour;
            note.style.wrap = true;
            y += style_.line_height * 2;
        }

        if (!shown.buttons.empty())
        {
            float x = 0;
            for (std::size_t i = 0; i < shown.buttons.size(); ++i)
            {
                const ManagementButton& b = shown.buttons[i];
                Button& button = lines_->add<Button>(std::format("button{}", i), style_.button, b.icon);
                button.text = b.text;
                button.text_inset = style_.button_inset;
                button.tooltip = b.help;
                button.enabled = b.enabled;
                const bool icon = b.text.empty();
                const float width = icon ? style_.icon_button_size : std::max(kNarrowestTextButton, static_cast<float>(b.text.size()) * kLetterWidth + kTextButtonMargin);
                const float height = icon ? style_.icon_button_size : style_.button_height;
                button.placement = bottom_left(x, 0, width, height);
                x += width + style_.gap / 2;
                buttons_.push_back(&button);
            }
        }

        // The lines' widget covers the whole page so its buttons can sit at the bottom; the table goes over the
        // rest.
        lines_->placement = fill();
        const bool has_table = !shown.columns.empty();
        table_->visible = has_table;
        if (!has_table)
            return;
        if (y > 0)
            y += style_.gap;
        table_->placement = { { 0, 0, 1, 1 }, { 0, y, 0, -bottom } };
        const auto selected = table_->selected() ? std::optional(table_->rows.at(*table_->selected()).id) : std::nullopt;
        table_->columns = shown.columns;
        table_->rows = shown.rows;
        page_sort_ = shown.sort ? std::optional(std::pair(*shown.sort, shown.sort_descending)) : std::nullopt;
        sort_rows();
        table_->select(std::nullopt);
        if (selected)
        {
            const auto found = std::ranges::find(table_->rows, *selected, &TableRow::id);
            if (found != table_->rows.end())
                table_->select(static_cast<std::size_t>(found - table_->rows.begin()));
        }
    }

    void ManagementWindow::sort_rows()
    {
        const auto sort = tab_ < sorts_.size() && sorts_[tab_] ? sorts_[tab_] : page_sort_;
        if (!sort || sort->first >= table_->columns.size())
        {
            table_->sorted_by.reset();
            return;
        }
        sort_table_rows(table_->rows, sort->first, sort->second);
        table_->sorted_by = sort->first;
        table_->descending = sort->second;
    }

    void ManagementWindow::update(const UpdateContext& context)
    {
        Window::update(context);
        page_age_ += context.elapsed;
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (!tabs_[i]->take_click())
                continue;
            if (i != tab_)
            {
                open_tab(i);
                tab_now_ = i;
                table_->bar().position = 0;
                table_->select(std::nullopt);
            }
            tabs_[i]->on = true; // Pressing the open tab leaves it open
        }
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click())
                button_now_ = i;
        }
        if (const auto column = table_->take_sort())
        {
            const auto now = table_->sorted_by;
            const bool flip = now == column;
            const std::pair<std::size_t, bool> sort { *column, flip ? !table_->descending : false };
            if (tab_ < sorts_.size())
                sorts_[tab_] = sort;
            const auto selected = table_->selected() ? std::optional(table_->rows.at(*table_->selected()).id) : std::nullopt;
            sort_rows();
            if (selected)
            {
                const auto found = std::ranges::find(table_->rows, *selected, &TableRow::id);
                table_->select(found != table_->rows.end() ? std::optional(static_cast<std::size_t>(found - table_->rows.begin())) : std::nullopt);
            }
        }
        if (table_->take_activate() && table_->selected())
            row_now_ = table_->rows.at(*table_->selected()).id;
    }

} // namespace rct3::ui
