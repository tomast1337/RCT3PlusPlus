// SPDX-License-Identifier: GPL-3.0-or-later
//
// A box that is ticked or not; a click, or Space or Enter while it has the keyboard focus, flips it. The tick skin is drawn over the box skin, both stretched
// over the widget.
#pragma once

#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{

    class Tickbox : public Widget
    {
    public:
        Tickbox(std::string name, std::string box, std::string tick);

        std::string box;
        std::string tick;
        bool ticked = false;

        /// True once for each flip since the last call.
        [[nodiscard]] bool take_change();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_release(Point p, bool inside) override;
        /// Space and Enter work like a click.
        bool on_key(const KeyPress& key) override;
        void draw(DrawContext& context) const override;

    private:
        void flip();

        int changes_ = 0;
    };

} // namespace rct3::ui
