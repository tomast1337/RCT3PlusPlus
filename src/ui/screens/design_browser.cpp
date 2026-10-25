// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/design_browser.hpp"

#include "ui/skin_library.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {
        constexpr std::size_t kCustomRow = 0; ///< Id of the row that builds a custom track; it stays first
    } // namespace

    DesignBrowser::DesignBrowser(std::string name, const DesignBrowserStyle& style, std::u16string title, std::string icon, std::vector<TableColumn> columns, std::vector<TableRow> rows, std::u16string build_text, std::u16string cancel_text)
        : Window(std::move(name), style.window, style.width, style.height)
        , style_(style)
    {
        this->title = std::move(title);
        this->icon = std::move(icon);
        Widget& body = content();
        const float bottom = style.button_height + style.gap;
        table_ = &body.add<ColumnTable>("list", style.table, style.scroll);
        table_->placement = { { 0, 0, style.list_share, 1 }, { 0, 0, -style.gap / 2, -bottom } };
        table_->columns = std::move(columns);
        table_->rows = std::move(rows);

        details_ = &body.add<Widget>("details");
        details_->placement = { { style.list_share, 0, 1, 1 }, { style.gap / 2, 0, 0, -bottom } };
        auto& picture = details_->add<Widget>("picture");
        picture.placement = { { 0.5f, 0, 0.5f, 0 }, { -style.picture / 2, 0, style.picture / 2, style.picture } };
        lines_ = &details_->add<Widget>("lines");
        lines_->placement = { { 0, 0, 1, 1 }, { 0, style.picture + style.gap, 0, -style.arrow - style.gap } };
        previous_ = &details_->add<Button>("previous_train", style.previous);
        previous_->placement = bottom_left(0, 0, style.arrow, style.arrow);
        next_ = &details_->add<Button>("next_train", style.next);
        next_->placement = bottom_right(0, 0, style.arrow, style.arrow);
        train_ = &details_->add<Label>("train", std::u16string { });
        train_->placement = { { 0, 1, 1, 1 }, { style.arrow + style.gap, -style.arrow, -style.arrow - style.gap, 0 } };
        train_->style.horizontal = Align::Centre;
        train_->style.vertical = Align::Centre;
        train_->style.colour = style.text_colour;

        build_button_ = &body.add<Button>("build", style.button);
        build_button_->text = std::move(build_text);
        build_button_->text_inset = style.button_inset;
        build_button_->placement = bottom_right(style.button_width + style.gap, 0, style.button_width, style.button_height);
        cancel_button_ = &body.add<Button>("cancel", style.button);
        cancel_button_->text = std::move(cancel_text);
        cancel_button_->text_inset = style.button_inset;
        cancel_button_->placement = bottom_right(0, 0, style.button_width, style.button_height);
        default_button = build_button_;
        cancel_button = cancel_button_;

        sort_table_rows(table_->rows, 0, false);
        std::ranges::stable_partition(table_->rows, [](const TableRow& r) { return r.id == kCustomRow; });
        table_->sorted_by = 0;
        table_->select(0);
        show_train({ }, false);
    }

    void DesignBrowser::show_design(TextureId picture, std::vector<InfoLine> lines, bool buildable)
    {
        picture_ = picture;
        while (!lines_->children().empty())
            (void)lines_->remove(*lines_->children().back());
        float y = 0;
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            InfoLine& l = lines[i];
            const bool both = !l.value.empty();
            Label& label = lines_->add<Label>(keyed_name(l.name, "label", i), std::move(l.label));
            label.placement = { { 0, 0, both ? style_.value_column : 1, 0 }, { 0, y, 0, y + style_.line_height } };
            label.style.colour = style_.text_colour;
            label.style.wrap = !both;
            if (both)
            {
                Label& value = lines_->add<Label>(keyed_name(l.name, "value", i), std::move(l.value));
                value.placement = { { style_.value_column, 0, 1, 0 }, { 0, y, 0, y + style_.line_height } };
                value.style.colour = style_.text_colour;
            }
            y += style_.line_height * (both ? 1 : 2);
        }
        build_button_->enabled = buildable;
    }

    void DesignBrowser::show_train(std::u16string name, bool steps)
    {
        train_->text = std::move(name);
        previous_->visible = steps;
        next_->visible = steps;
    }

    void DesignBrowser::update(const UpdateContext& context)
    {
        Window::update(context);
        if (const auto column = table_->take_sort())
        {
            const bool descending = table_->sorted_by == column && !table_->descending;
            const auto selected = table_->selected() ? std::optional(table_->rows.at(*table_->selected()).id) : std::nullopt;
            sort_table_rows(table_->rows, *column, descending);
            std::ranges::stable_partition(table_->rows, [](const TableRow& r) { return r.id == kCustomRow; });
            table_->sorted_by = column;
            table_->descending = descending;
            const auto found = selected ? std::ranges::find(table_->rows, *selected, &TableRow::id) : table_->rows.end();
            table_->select(found == table_->rows.end() ? std::nullopt : std::optional(static_cast<std::size_t>(found - table_->rows.begin())));
        }
        const auto selected = table_->selected() ? std::optional(table_->rows.at(*table_->selected()).id) : std::nullopt;
        if (selected != shown_)
        {
            shown_ = selected;
            if (selected)
                picked_ = selected;
        }
        if (previous_->take_click())
            train_step_ = -1;
        if (next_->take_click())
            train_step_ = 1;
        if ((build_button_->take_click() || table_->take_activate()) && build_button_->enabled && shown_)
            build_ = shown_;
        if (cancel_button_->take_click())
            cancel_ = true;
    }

    void DesignBrowser::draw(DrawContext& context) const
    {
        Window::draw(context);
        if (picture_ == 0 || !details_->visible)
            return;
        const auto picture = details_->find("picture");
        if (!picture)
            return;
        const Rect& r = picture->rect();
        draw_skin(context.list, context.skins.find(style_.border), r);
        context.list.image({ r.x0 + 2, r.y0 + 2, r.x1 - 2, r.y1 - 2 }, picture_, { 0, 0, 1, 1 });
    }

} // namespace rct3::ui
