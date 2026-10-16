// SPDX-License-Identifier: GPL-3.0-or-later
//
// A list of rows of several columns under a band of headings, with a scroll bar on the right for when the rows
// do not all fit. Pressing a heading asks for the rows to be sorted by that column, and pressing it again asks
// for the other direction; the owner sorts (see sort_table_rows) and the table marks the column and direction
// it was told. Pressing a row selects it, and pressing it again quickly (a double click) activates it. The
// wheel scrolls.
//
// The owner asks with take_sort() whether the user pressed a heading, and with take_activate() whether they
// activated a row. Changing the rows or the sort is never reported.
#pragma once

#include "ui/text_layout.hpp"
#include "ui/widgets/scroll_bar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct TableColumn
    {
        std::u16string heading;
        float width = 0; ///< Units; 0 shares out what the fixed columns leave
        Align align = Align::Start;
        bool sortable = true;
    };

    /// One row: a text for each column, and for each column a number to sort by, when the text is not what
    /// orders it (money, counts, percentages). A row without a number for a column sorts by its text there.
    struct TableRow
    {
        std::vector<std::u16string> cells;
        std::vector<std::optional<double>> keys;
        std::size_t id = 0; ///< The owner's, to know which thing the row is about after sorting
    };

    struct ColumnTableStyle
    {
        std::string background;     ///< Stretched behind the rows; none when empty
        std::string selection_skin; ///< Stretched behind the selected row; the colour is filled when empty
        Color selection_colour = hex("#5aa8e6");
        Color heading_colour = hex("#2384bf");
        Color heading_text_colour = hex("#ffffff");
        Color text_colour = hex("#000000");
        Color stripe_colour = hex("#00000010"); ///< Over every other row
        FontRole font = FontRole::Small;
        FontRole heading_font = FontRole::Small;
        float heading_height = 18;
        float row_height = 16;
        Rect padding { 3, 3, 3, 3 }; ///< Between the edges and the headings, rows and bar
        float cell_indent = 4;       ///< Inside each cell, on the side its text keeps to
        float sort_mark = 7;         ///< Width of the triangle that marks the sorted column
        float double_click = 0.4f;   ///< Seconds within which a second press activates
    };

    /// Sorts `rows` by `column`, by its numbers where both rows have one and by its text otherwise, rows with a
    /// number first; ties keep their order. Lowest first, or highest first when `descending`.
    void sort_table_rows(std::vector<TableRow>& rows, std::size_t column, bool descending);

    class ColumnTable : public Widget
    {
    public:
        ColumnTable(std::string name, ColumnTableStyle style, ScrollBarStyle bar);

        ColumnTableStyle style;
        std::vector<TableColumn> columns;
        std::vector<TableRow> rows;
        std::optional<std::size_t> sorted_by; ///< Marked in its heading
        bool descending = false;

        /// The left and right edges of each column, from the table's left edge.
        [[nodiscard]] std::vector<std::pair<float, float>> column_edges() const;

        [[nodiscard]] std::optional<std::size_t> selected() const
        {
            return selected_;
        }
        void select(std::optional<std::size_t> index);
        [[nodiscard]] ScrollBar& bar()
        {
            return *bar_;
        }
        /// The row at `p`, if `p` is over one.
        [[nodiscard]] std::optional<std::size_t> row_at(Point p) const;
        /// The heading at `p`, if `p` is over one.
        [[nodiscard]] std::optional<std::size_t> heading_at(Point p) const;

        /// The column whose heading the user pressed, once.
        [[nodiscard]] std::optional<std::size_t> take_sort();
        /// True once for each row activated since the last call.
        [[nodiscard]] bool take_activate();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void on_press(Point p) override;
        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] Rect headings_box() const;
        [[nodiscard]] Rect rows_box() const;

        ScrollBar* bar_ = nullptr;
        std::optional<std::size_t> selected_;
        std::optional<std::size_t> sort_now_;
        float since_press_ = 1e9f; // Seconds since the last press on a row
        std::optional<std::size_t> last_pressed_;
        int activations_ = 0;
    };

} // namespace rct3::ui
