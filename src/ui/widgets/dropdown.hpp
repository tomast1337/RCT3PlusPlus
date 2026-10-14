// SPDX-License-Identifier: GPL-3.0-or-later
//
// A drop-down list: a field showing the selected item, with an opener arrow on its right. Pressing it opens
// a menu of the items below it, the selected one ticked and highlighted; the menu can be chosen from by
// dragging to a row and releasing, or by pressing again. Pressing the field while the menu is open closes
// it.
//
// With the keyboard focus, Up and Down select the item before or after without opening, Home and End the
// first and last, and Enter or Space opens the menu (where the menu's own keys apply).
//
// The owner asks with take_change() whether the user changed the selection.
#pragma once

#include "ui/widgets/menu.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct DropdownStyle
    {
        std::string field;             ///< Behind the selected text
        std::string focused;           ///< Instead of `field` while the list has the keyboard focus
        std::string opener;            ///< Family base name for the arrow on the right (see SkinLibrary)
        Color fill = hex("#00000000"); ///< Behind the field skin, if any
        bool opener_in_corner = false; ///< The arrow in the top-right corner instead of inset and centred
        Color text_colour = hex("#000000");
        Color disabled_text_colour = hex("#7f7f7f");
        FontRole font = FontRole::Small;
        float opener_width = 16; ///< The arrow is a square this wide at the right edge, centred vertically
        float text_indent = 5;
        MenuStyle menu;
    };

    class Dropdown : public Widget
    {
    public:
        Dropdown(std::string name, DropdownStyle style);
        ~Dropdown() override;
        Dropdown(const Dropdown&) = delete;
        Dropdown& operator=(const Dropdown&) = delete;
        Dropdown(Dropdown&&) = delete;
        Dropdown& operator=(Dropdown&&) = delete;

        DropdownStyle style;
        std::vector<std::u16string> items;

        [[nodiscard]] std::optional<std::size_t> selected() const
        {
            return selected_;
        }
        /// Selects `index`, or nothing when out of range. Not reported by take_change().
        void select(std::optional<std::size_t> index);

        /// True once for each change by the user since the last call.
        [[nodiscard]] bool take_change();

        /// The open menu, if any; it lives on the desktop's root.
        [[nodiscard]] Menu* menu() const
        {
            return menu_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool clicks() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void open();
        void pick(std::size_t index);

        std::optional<std::size_t> selected_;
        Menu* menu_ = nullptr; // Valid until this widget's update sees it closing; the sweep comes after
        bool dragged_into_menu_ = false;
        int changes_ = 0;
    };

} // namespace rct3::ui
