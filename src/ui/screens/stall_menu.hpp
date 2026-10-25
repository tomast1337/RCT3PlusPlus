// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stall's menu, as its Items page shows it. Down the left, a strip of the goods the stall's kind can sell,
// stacked upwards from a bin in the bottom corner, a new column beside when they run past the top; right of
// the strip, the tick that keeps every stall of the kind alike under a scrolling pane of the menu itself.
//
// The menu is a row for each item on it, in its order: a handle with the item's picture, its name to type
// over, a pot for the items that can be painted, a tick for each extra the item can take and the stall
// offers, and its price. Over each run of items that take the same extras, a light band shows the extras'
// pictures over their ticks.
//
// Goods go on by dragging them from the strip onto the pane, where they drop between the rows, or by a
// click, which puts them at the end. An item moves by dragging its handle to another place, and goes off by
// dropping it on the bin; a click on a handle picks the item, and a click on the bin then takes it off.
#pragma once

#include "ui/widgets/paint_button.hpp"
#include "ui/widgets/row_bands.hpp"
#include "ui/widgets/scroll_bar.hpp"
#include "ui/widgets/spinner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    class EditBox;
    class ScrollArea;
    class Tickbox;

    /// Something the menu offers or shows: its name and its picture.
    struct MenuGood
    {
        std::u16string name;
        std::string icon;
        std::u16string help; ///< Shown when the pointer rests on it
    };

    /// An item on the menu.
    struct MenuLine
    {
        std::u16string name;
        std::string icon;
        int price = 0; ///< Hundredths
        /// The extras the item can take, in its own order, each by place in StallMenuView::extras; none where
        /// the stall does not offer it.
        std::vector<std::optional<std::size_t>> extras;
        std::uint32_t ticked = 0;              ///< Bit i: extras[i] goes on
        std::vector<std::size_t> paints;       ///< The colours of its pots; none when it cannot be painted
        std::u16string help;                   ///< Shown when the pointer rests on its row, as the profit on it
        std::vector<std::u16string> tick_help; ///< Of each of its extras' ticks
    };

    /// What the player does to the menu.
    enum class MenuAction : std::uint8_t
    {
        Add,    ///< Puts goods[index] on the menu at place `value`
        Remove, ///< Takes lines[index] off
        Move,   ///< Moves lines[index] to place `value`, counted once it is out
        Price,  ///< Of lines[index], `value` hundredths
        Extras, ///< The ticks of lines[index], as MenuLine::ticked
        Name,   ///< Of lines[index], `text`
        Sync,   ///< Whether the stall's kind is kept alike, `value` 0 or 1
        Colour, ///< Of lines[index]: picked through its pots, as the page's other pots are
    };
    inline constexpr std::size_t kMenuActions = 8;

    struct MenuEvent
    {
        MenuAction action = MenuAction::Add;
        std::size_t index = 0;
        int value = 0;
        std::u16string text;

        bool operator==(const MenuEvent&) const = default;
    };

    struct StallMenuView
    {
        std::vector<MenuGood> goods;  ///< In the strip
        std::vector<MenuGood> extras; ///< The stall's extras; each one's help says how much of it goes on
        std::vector<MenuLine> lines;
        bool sync = false;
        std::u16string sync_help;
        std::u16string bin_help;
        std::u16string handle_help;
        /// The page's control id of each action, by MenuAction; a line's or good's is that and its place.
        std::array<int, kMenuActions> ids { };
        int most_price = 0;
        int price_step = 1;
        std::size_t longest_name = 0; ///< In characters
    };

    struct StallMenuStyle
    {
        RowBands rows;
        ScrollBarStyle bar;
        SpinnerStyle price;
        PaintButtonStyle paint;
        std::string tick_box;
        std::string tick;
        std::string bin;             ///< The bin's picture
        Color picked;                ///< Behind a line picked by a click on its handle
        Color drop = hex("#000000"); ///< The mark where a dragged item would drop
        float strip_cell = 16;       ///< A good's square in the strip, and the bin's
        float price_width = 96;      ///< Of a line's price, at its right
        float extras_width = 96;     ///< Of a line's extras and pot, left of its price
        float pot_width = 20;        ///< Of its pot, at the left of its extras
        float tick_gap = 3;          ///< Between the tick and the pane over it
        float drag_start = 4;        ///< How far the pointer goes before a press drags
        Color text_colour = hex("#000000");
        Color help_colour = hex("#ffffff");
    };

    /// A row of the menu: a band over a run of lines, or a line.
    struct MenuRow
    {
        std::optional<std::size_t> line; ///< None for a band
        std::vector<std::size_t> extras; ///< Of a band: the extras over its ticks, by place in the view's
    };

    /// The rows `view`'s lines make: a band before each run of lines with the same extras on offer, when they
    /// offer any.
    [[nodiscard]] std::vector<MenuRow> menu_rows(const StallMenuView& view);

    /// The place a line goes when one dragged from `from` (none for a new one) drops before line `before`.
    [[nodiscard]] std::size_t drop_place(std::optional<std::size_t> from, std::size_t before);

    class StallMenu : public Widget
    {
    public:
        StallMenu(std::string name, StallMenuStyle style, StallMenuView view);

        [[nodiscard]] const StallMenuView& view() const
        {
            return view_;
        }
        /// What the player did since the last call, once.
        [[nodiscard]] std::optional<MenuEvent> take_event();
        /// The pots of the lines that can be painted, each with its line.
        [[nodiscard]] std::vector<std::pair<std::size_t, PaintButton*>> pots() const;
        /// The pane's scroll position, to keep when the menu is shown anew.
        [[nodiscard]] float scrolled() const;
        void scroll_to(float position);

        /// Where good `i` sits in the strip, and the bin, on the screen.
        [[nodiscard]] Rect good_rect(std::size_t i) const;
        [[nodiscard]] Rect bin_rect() const;
        /// Where the handle of line `i` is on the screen, scrolled as it is.
        [[nodiscard]] Rect handle_rect(std::size_t i) const;
        [[nodiscard]] Rect pane_rect() const;
        [[nodiscard]] std::optional<std::size_t> picked() const
        {
            return picked_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] CursorKind cursor(Point p) const override;
        [[nodiscard]] CursorKind held_cursor(Point p) const override;
        void on_hover(Point p) override;
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

        // Used by the pane's rows, which take the presses on the handles.
        void press_handle(std::size_t line, Point p);
        void draw_rows(DrawContext& context, const Rect& content) const;
        void draw_drag(DrawContext& context) const;

    private:
        struct Drag
        {
            std::optional<std::size_t> good; ///< Dragged from the strip
            std::optional<std::size_t> line; ///< Dragged by its handle
            Point from;
            Point at;
            bool moving = false;
        };
        struct LineWidgets
        {
            EditBox* name = nullptr;
            Spinner* price = nullptr;
            std::vector<Tickbox*> ticks; ///< By place in the line's extras; null where it has none
            PaintButton* pot = nullptr;
        };

        /// Which strip cell `p` is on: a good, or the bin when none.
        [[nodiscard]] std::optional<std::size_t> good_at(Point p) const;
        /// The line a drop at `p` goes before, when `p` is on the pane.
        [[nodiscard]] std::optional<std::size_t> drop_before(Point p) const;
        [[nodiscard]] std::size_t strip_rows() const;
        [[nodiscard]] float strip_width() const;
        void drop(Point p);
        void lay_out_rows();

        StallMenuStyle style_;
        StallMenuView view_;
        std::vector<MenuRow> rows_;
        ScrollArea* pane_ = nullptr;
        Widget* back_ = nullptr;
        Tickbox* sync_ = nullptr;
        Widget* over_ = nullptr; ///< Draws what is dragged over the rest
        std::vector<LineWidgets> lines_;
        std::optional<Drag> drag_;
        std::optional<std::size_t> picked_;
        bool bin_pressed_ = false;
        std::optional<MenuEvent> event_;
    };

} // namespace rct3::ui
