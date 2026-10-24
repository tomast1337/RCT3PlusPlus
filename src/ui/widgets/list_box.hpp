// SPDX-License-Identifier: GPL-3.0-or-later
//
// A list of text rows to pick one from, with a scroll bar on the right for when they do not all fit. A row may
// start with small pictures, and may be greyed: faded, and passed over by every way of picking. Pressing a
// row selects it, dragging moves the selection along, and pressing the same row again quickly (a double
// click) activates it. The wheel scrolls.
//
// Pressing the list gives it the keyboard focus. Then Up and Down move the selection by a row, Page Up and
// Page Down by a view less one row, Home and End to the ends, and Enter activates the selected row. Typing a
// character selects the next row that starts with it, so pressing the same letter again cycles through them
// (letters a to z match either case). The selection is always scrolled into view.
//
// The owner asks with take_change() whether the user changed the selection, and with take_activate() whether
// they activated a row. Changing `items` or calling select() is never reported.
#pragma once

#include "ui/widgets/scroll_bar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct ListBoxStyle
    {
        std::string background;     ///< Stretched behind the rows; none when empty
        std::string focused;        ///< Instead of the background while the list has the keyboard focus
        std::string selection_skin; ///< Stretched behind the selected row; the colour is filled when empty
        Color selection_colour = hex("#5aa8e6");
        Color text_colour = hex("#000000");
        Color disabled_text_colour = hex("#7f7f7f");
        Color stripe = hex("#00000000");      ///< Behind every other row, the first included
        Color greyed_tint = hex("#ffffff66"); ///< Over a greyed row's pictures
        float icon_size = 16;                 ///< Of the pictures before a row's text, each square
        float icon_gap = 1;                   ///< Between the pictures, and after the last
        FontRole font = FontRole::Small;
        float row_height = 18;
        Rect padding { 3, 3, 3, 3 }; ///< Between the edges and the rows and bar; fixed when the list is made
        float text_indent = 4;       ///< From a row's left edge to its text
        float double_click = 0.4f;   ///< Seconds within which a second press activates
    };

    class ListBox : public Widget
    {
    public:
        ListBox(std::string name, ListBoxStyle style, ScrollBarStyle bar);

        ListBoxStyle style;
        std::vector<std::u16string> items;
        std::vector<std::vector<std::string>> icons; ///< Skins of the pictures before each row's text, by row
        std::vector<bool> greyed;                    ///< By row; rows past its end are not

        [[nodiscard]] std::optional<std::size_t> selected() const
        {
            return selected_;
        }
        /// Selects `index` (nothing when out of range or nullopt) and scrolls it into view.
        void select(std::optional<std::size_t> index);

        [[nodiscard]] ScrollBar& bar()
        {
            return *bar_;
        }
        /// The row at `p`, if `p` is over one.
        [[nodiscard]] std::optional<std::size_t> row_at(Point p) const;

        /// True once for each change of the selection by the user since the last call.
        [[nodiscard]] bool take_change();
        /// True once for each row activated since the last call.
        [[nodiscard]] bool take_activate();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        /// A hand over the rows, which pick when pressed.
        [[nodiscard]] CursorKind cursor(Point p) const override
        {
            return row_at(p) ? CursorKind::Hand : CursorKind::Arrow;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        bool on_wheel(Point notches) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] bool is_greyed(std::size_t row) const
        {
            return row < greyed.size() && greyed[row];
        }
        [[nodiscard]] Rect rows_box() const;
        [[nodiscard]] std::size_t rows_in_view() const;
        void pick(std::size_t index);

        ScrollBar* bar_ = nullptr;
        std::optional<std::size_t> selected_;
        bool reveal_ = false;      // Scroll the selection into view at the next update
        float since_press_ = 1e9f; // Seconds since the last press on a row
        std::optional<std::size_t> last_pressed_;
        int changes_ = 0;
        int activations_ = 0;
    };

} // namespace rct3::ui
