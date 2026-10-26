// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text in a box, in one of the interface's font roles. Lets the pointer through, unless it is copyable: then
// pressing it selects all its text, shown on a tint, for Control+C to copy. Each update measures the text at
// the box's width, so a scroll area can tell how far wrapped text runs below the box.
#pragma once

#include "ui/font_set.hpp"
#include "ui/text_layout.hpp"
#include "ui/widget.hpp"

#include <optional>
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
        bool copyable = false;
        Color selection_colour = hex("#007fff50"); ///< Behind the text while selected, as in an edit box

        [[nodiscard]] bool takes_pointer() const override
        {
            return copyable;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return copyable;
        }
        [[nodiscard]] CursorKind cursor(Point) const override
        {
            return copyable ? CursorKind::Text : CursorKind::Arrow;
        }
        bool on_key(const KeyPress& key) override;
        [[nodiscard]] std::optional<std::u16string> take_copied() override;
        void update(const UpdateContext& context) override;
        [[nodiscard]] Rect extent() const override;
        void draw(DrawContext& context) const override;

    private:
        Point text_size_; // Widest line and all lines' height, as of the last update
        std::optional<std::u16string> copied_;
    };

} // namespace rct3::ui
