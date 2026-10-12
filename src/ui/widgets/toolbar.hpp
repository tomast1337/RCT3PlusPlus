// SPDX-License-Identifier: GPL-3.0-or-later
//
// A strip of icon buttons, laid out in a row (or a column) in the order they were added, with gaps and
// separator lines where asked. The buttons are ordinary buttons: the owner keeps them and asks each for its
// clicks, and may make any of them a toggle.
#pragma once

#include "ui/widgets/button.hpp"
#include "ui/widgets/scroll_bar.hpp"

#include <string>

namespace rct3::ui
{

    struct ToolbarStyle
    {
        std::string background; ///< Stretched behind the buttons; none when empty
        std::string button;     ///< Family base name for the buttons (see SkinLibrary)
        Color separator_colour { 0, 0, 0, 60 };
        Rect padding { 4, 4, 4, 4 }; ///< Between the edges and the buttons
        float button_size = 30;      ///< Buttons are squares
        float gap = 4;               ///< Between buttons
        float separator_room = 12;   ///< Taken by a separator, its line in the middle
        float swell = 0;             ///< Of the buttons; see Button
    };

    class Toolbar : public Widget
    {
    public:
        Toolbar(std::string name, ToolbarStyle style, Orientation orientation = Orientation::Horizontal);

        /// Appends a button showing `icon`, with `tooltip` as its help.
        Button& add_button(std::string name, std::string icon, std::u16string tooltip = { });
        /// Appends a separator line.
        void add_separator();

        /// The length along the bar that its buttons and separators take, padding included.
        [[nodiscard]] float length() const;

        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void arrange();

        ToolbarStyle style_;
        Orientation orientation_;
        int separators_ = 0;
    };

} // namespace rct3::ui
