// SPDX-License-Identifier: GPL-3.0-or-later
//
// A box that is ticked or not; a click flips it. The tick skin is drawn over the box skin, both stretched
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
        void on_release(Point p, bool inside) override;
        void draw(DrawContext& context) const override;

    private:
        int changes_ = 0;
    };

} // namespace rct3::ui
