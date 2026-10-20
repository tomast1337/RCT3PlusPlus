// SPDX-License-Identifier: GPL-3.0-or-later
//
// A row of menu titles; pressing one opens its menu below it. While a menu is open, moving the pointer
// over another title opens that one instead, and Left and Right in the menu move to the menu beside it.
// Pressing the open menu's title again closes it.
//
// With the keyboard focus on the bar, Left and Right move between titles and Enter, Space or Down open the
// one marked.
//
// The owner asks with take_choice() which command was chosen: the menu's index and the row's.
#pragma once

#include "core/maybe.hpp"
#include "ui/widgets/menu.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct MenuBarStyle
    {
        std::string background;     ///< Stretched behind the titles; none when empty
        std::string highlight_skin; ///< Behind the open or marked title; the colour is filled when empty
        Color highlight_colour = hex("#5aa8e6");
        Color text_colour = hex("#000000");
        FontRole font = FontRole::Small;
        float title_padding = 10; ///< Each side of a title's text
        float first_indent = 4;
        MenuStyle menu;
    };

    struct MenuBarEntry
    {
        std::u16string title;
        std::vector<MenuItem> items;
    };

    struct MenuChoice
    {
        std::size_t menu = 0;
        std::size_t item = 0;

        friend bool operator==(const MenuChoice&, const MenuChoice&) = default;
    };

    class MenuBar : public Widget
    {
    public:
        MenuBar(std::string name, MenuBarStyle style);
        ~MenuBar() override;
        MenuBar(const MenuBar&) = delete;
        MenuBar& operator=(const MenuBar&) = delete;
        MenuBar(MenuBar&&) = delete;
        MenuBar& operator=(MenuBar&&) = delete;

        MenuBarStyle style;
        std::vector<MenuBarEntry> menus;

        /// The command chosen, once.
        [[nodiscard]] std::optional<MenuChoice> take_choice()
        {
            return std::exchange(choice_, std::nullopt);
        }

        /// Which menu is open, if any.
        [[nodiscard]] std::optional<std::size_t> open_menu() const
        {
            return menu_ != nullptr ? std::optional(open_) : std::nullopt;
        }
        [[nodiscard]] std::optional<Menu&> menu() const
        {
            return maybe(menu_);
        }
        /// Where title `index` is, in desktop units, as of the last update.
        [[nodiscard]] Rect title_rect(std::size_t index) const;
        [[nodiscard]] std::optional<std::size_t> title_at(Point p) const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_press(Point p) override;
        void on_hover(Point p) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void open(std::size_t index);
        void close_menu();

        std::vector<float> widths_; // Of each title, from the last update with fonts
        Menu* menu_ = nullptr;      // Valid until this widget's update sees it closing
        std::size_t open_ = 0;
        std::size_t marked_ = 0; // The title the keys move between
        std::optional<MenuChoice> choice_;
    };

} // namespace rct3::ui
