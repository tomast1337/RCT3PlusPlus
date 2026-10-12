// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text in a box, in one of the interface's font roles. Lets the pointer through. Each update measures the
// text at the box's width, so a scroll area can tell how far wrapped text runs below the box.
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

        void update(const UpdateContext& context) override;
        [[nodiscard]] Rect extent() const override;
        void draw(DrawContext& context) const override;

    private:
        float text_height_ = 0; // As of the last update
    };

} // namespace rct3::ui
