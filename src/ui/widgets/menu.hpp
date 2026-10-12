// SPDX-License-Identifier: GPL-3.0-or-later
//
// A popup list of commands, as opened by a menu bar or a drop-down list. It is added to the desktop's root
// by whoever opens it, sizes itself to its items once it has fonts, and closes when a command is chosen,
// when Escape reaches it, or (as a popup, see Desktop) when a press misses it.
//
// The row under the pointer is highlighted; releasing the pointer on an enabled row chooses it, so a menu
// can be opened with a press and chosen from by dragging to a row. With the keyboard, Up and Down move the
// highlight over the enabled rows, Home and End go to the first and last, and Enter or Space chooses. Left
// and Right are kept for a menu bar to move to the next menu (take_sideways()).
//
// The opener asks with take_choice(), in its update, which row was chosen; the menu is gone after the
// update in which it closed.
#pragma once

#include "ui/widget.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct MenuItem
    {
        std::u16string text { };
        std::u16string shortcut { }; ///< Shown on the right, e.g. "Ctrl+S"; the opener handles the keys
        bool enabled = true;
        bool checked = false;   ///< A tick before the text
        bool separator = false; ///< A line instead of a command
    };

    struct MenuStyle
    {
        std::string background;     ///< Stretched behind the rows
        std::string highlight_skin; ///< Behind the highlighted row; the colour is filled when empty
        std::string tick;           ///< Before checked rows; a filled square when empty
        Color highlight_colour { 90, 168, 230, 255 };
        Color text_colour { 0, 0, 0, 255 };
        Color disabled_text_colour { 127, 127, 127, 255 };
        Color separator_colour { 0, 0, 0, 60 };
        FontRole font = FontRole::Small;
        Rect padding { 6, 6, 6, 6 }; ///< Between the edges and the rows
        float row_height = 18;
        float separator_height = 7;
        float tick_room = 16;    ///< Left of the text, for the tick
        float shortcut_gap = 24; ///< Between the text and the shortcut
        float text_end_room = 8; ///< Right of the widest text or shortcut
        float min_width = 80;
    };

    class Menu : public Widget
    {
    public:
        Menu(std::string name, MenuStyle style, std::vector<MenuItem> items);

        MenuStyle style;
        std::vector<MenuItem> items;

        /// Opens the menu with its top-left corner at `at` in its parent (the desktop's root), at least
        /// `min_width` wide. Its height follows the items now; its width once it has fonts.
        void place_at(Point at, float min_width = 0);

        [[nodiscard]] std::optional<std::size_t> highlighted() const
        {
            return highlighted_;
        }
        /// Highlights `index` when it is an enabled command, or nothing.
        void highlight(std::optional<std::size_t> index);

        /// The row chosen, once.
        [[nodiscard]] std::optional<std::size_t> take_choice()
        {
            return std::exchange(choice_, std::nullopt);
        }
        /// -1 for Left and +1 for Right since the last call, added up.
        [[nodiscard]] int take_sideways()
        {
            return std::exchange(sideways_, 0);
        }

        /// The row at `p`, separators included.
        [[nodiscard]] std::optional<std::size_t> row_at(Point p) const;
        /// Where row `index` is drawn, in desktop units.
        [[nodiscard]] Rect row_rect(std::size_t index) const;
        /// The height the items take, padding included.
        [[nodiscard]] float content_height() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_hover(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] bool choosable(std::size_t index) const;
        void move_highlight(int direction, std::size_t from);
        void choose(std::size_t index);

        Point at_;
        float min_width_ = 0;
        std::optional<std::size_t> highlighted_;
        std::optional<std::size_t> choice_;
        int sideways_ = 0;
    };

} // namespace rct3::ui
