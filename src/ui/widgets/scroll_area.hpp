// SPDX-License-Identifier: GPL-3.0-or-later
//
// A view onto content bigger than itself, with a scroll bar on the right, along the bottom, or both. Children
// go into content(); the area measures how far they reach (wrapped text included, see Widget::extent) and
// lets each bar scroll that far. Whatever falls outside the view is cut off.
//
// Along a scrolled direction the content is at least as long as the view and grows to what its children
// reach; along the other it matches the view. So children stretched to the content's edges follow the view,
// and only children that are bigger than it make it scroll.
//
// The wheel over the area scrolls it: turned normally vertically, turned sideways (or with Shift)
// horizontally. An area that only scrolls horizontally takes either.
//
// Pressing inside the area gives it the keyboard focus. Then the arrow keys move by a bar's arrow step, Page
// Up and Page Down by a view's length less one step, so a line stays in sight, and Home and End go to the
// ends. Up and down move the horizontal bar when there is no vertical one.
#pragma once

#include "core/maybe.hpp"
#include "ui/widgets/scroll_bar.hpp"

#include <optional>
#include <string>

namespace rct3::ui
{

    class ScrollArea : public Widget
    {
    public:
        /// A bar for each direction given a style.
        ScrollArea(std::string name, std::optional<ScrollBarStyle> vertical, std::optional<ScrollBarStyle> horizontal = std::nullopt);

        [[nodiscard]] Widget& content()
        {
            return *content_;
        }
        /// Nothing when the area does not scroll that way.
        [[nodiscard]] std::optional<ScrollBar&> vertical()
        {
            return maybe(vertical_);
        }
        [[nodiscard]] std::optional<ScrollBar&> horizontal()
        {
            return maybe(horizontal_);
        }

        /// So a press on empty parts of the view stays in the interface and focuses the area.
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        bool on_wheel(Point notches) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;

    private:
        void place_content();

        Widget* view_ = nullptr; // The part beside the bars, cutting off the content
        Widget* content_ = nullptr;
        ScrollBar* vertical_ = nullptr;
        ScrollBar* horizontal_ = nullptr;
    };

} // namespace rct3::ui
