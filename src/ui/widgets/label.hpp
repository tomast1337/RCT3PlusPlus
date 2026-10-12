// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text in a box, in one of the interface's font roles. Lets the pointer through.
#pragma once

#include "ui/font_set.hpp"
#include "ui/text_layout.hpp"
#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{

    class Label : public Widget
    {
    public:
        Label(std::string name, std::u16string text, FontRole font = FontRole::Small);

        std::u16string text;
        FontRole font;
        TextStyle style;

        void draw(DrawContext& context) const override;
    };

} // namespace rct3::ui
