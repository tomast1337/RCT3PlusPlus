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
#pragma once

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
        [[nodiscard]] ScrollBar* vertical()
        {
            return vertical_;
        }
        [[nodiscard]] ScrollBar* horizontal()
        {
            return horizontal_;
        }

        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;

    private:
        void place_content();

        Widget* view_ = nullptr; // The part beside the bars, cutting off the content
        Widget* content_ = nullptr;
        ScrollBar* vertical_ = nullptr;
        ScrollBar* horizontal_ = nullptr;
    };

} // namespace rct3::ui
