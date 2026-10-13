// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pages that share one place, with a row of tabs above them to pick which shows. Each tab is a button with
// the page's label, as wide as its text needs; the current one is drawn down. Only the current page is
// visible, so the others take no input.
//
// Control+Tab goes to the next page and Control+Shift+Tab to the previous one, from anywhere inside the
// tabs; other keys for those commands arrive as these (see key_map.hpp). Left and Right do the same while a tab has the keyboard
// focus.
//
// The owner asks with take_change() whether the user changed the page.
#pragma once

#include "ui/widgets/button.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct TabsStyle
    {
        std::string tab; ///< Family base name for the tab buttons (see SkinLibrary)
        Rect text_inset; ///< Taken off each tab before centring its label
        FontRole font = FontRole::Small;
        float bar_height = 24;
        float gap = 4;        ///< Between tabs
        float text_room = 20; ///< Added to the label's width
        float min_tab_width = 50;
        float page_gap = 6; ///< Between the tabs and the pages
    };

    /// A tab: a button that also takes Left and Right, for its Tabs to turn the page by.
    class TabButton : public Button
    {
    public:
        using Button::Button;

        /// -1 for each Left and +1 for each Right since the last call, added up.
        [[nodiscard]] int take_turn()
        {
            return std::exchange(turn_, 0);
        }
        bool on_key(const KeyPress& key) override;

    private:
        int turn_ = 0;
    };

    class Tabs : public Widget
    {
    public:
        Tabs(std::string name, TabsStyle style);

        /// Adds a page with a tab labelled `label` and returns it for its children. The first page added
        /// shows.
        Widget& add_page(std::string name, std::u16string label);

        [[nodiscard]] std::size_t count() const
        {
            return pages_.size();
        }
        [[nodiscard]] std::size_t current() const
        {
            return current_;
        }
        /// Shows page `index`; nothing when out of range. Not reported by take_change().
        void show(std::size_t index);

        [[nodiscard]] Widget& page(std::size_t index)
        {
            return *pages_.at(index);
        }
        [[nodiscard]] TabButton& tab(std::size_t index)
        {
            return *tabs_.at(index);
        }

        /// True once for each page change by the user since the last call.
        [[nodiscard]] bool take_change();

        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;

    private:
        void turn(int by); // By the user, wrapping round

        TabsStyle style_;
        Widget* bar_ = nullptr;
        std::vector<TabButton*> tabs_;
        std::vector<Widget*> pages_;
        std::size_t current_ = 0;
        int changes_ = 0;
    };

} // namespace rct3::ui
