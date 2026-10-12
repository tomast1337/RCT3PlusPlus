// SPDX-License-Identifier: GPL-3.0-or-later
//
// A view onto content taller than itself, with a vertical scroll bar on the right. Children go into
// content(), which is as wide as the view minus the bar and at least as tall as the view; the area measures
// how far they reach (wrapped text included, see Widget::extent) and lets the bar scroll that far. Whatever
// falls outside the view is cut off. The wheel over the area scrolls it.
#pragma once

#include "ui/widgets/scroll_bar.hpp"

#include <string>

namespace rct3::ui
{

    class ScrollArea : public Widget
    {
    public:
        ScrollArea(std::string name, ScrollBarStyle bar_style);

        [[nodiscard]] Widget& content()
        {
            return *content_;
        }
        [[nodiscard]] ScrollBar& bar()
        {
            return *bar_;
        }
        [[nodiscard]] const ScrollBar& bar() const
        {
            return *bar_;
        }

        bool on_wheel(float notches) override;
        void update(const UpdateContext& context) override;

    private:
        Widget* content_ = nullptr;
        ScrollBar* bar_ = nullptr;
    };

} // namespace rct3::ui
