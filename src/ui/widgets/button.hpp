// SPDX-License-Identifier: GPL-3.0-or-later
//
// A push button, or a toggle that stays down until clicked again. Drawn with a skin family (see
// SkinLibrary), with an optional icon centred on top at its own size and optional text centred over that.
// When the family has no member for the current state the base skin is tinted instead, so plain skins still
// show presses. Text is black, grey when the button is disabled, as in the game.
//
// A click is a press and release over the button, or Space or Enter while it has the keyboard focus. The
// owner asks for it with take_click().
#pragma once

#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{

    class Button : public Widget
    {
    public:
        Button(std::string name, std::string skin, std::string icon = { });

        std::string skin; ///< Family base name
        std::string icon; ///< Optional skin drawn on top
        std::u16string text;
        FontRole font = FontRole::Small;
        Color text_colour { 0, 0, 0, 255 };
        Color disabled_text_colour { 127, 127, 127, 255 };
        Rect text_inset; ///< Taken off each side before centring the text, e.g. to leave out a skin's shadow
        bool toggle = false;
        bool on = false; ///< Toggles only

        /// True once for each click since the last call.
        [[nodiscard]] bool take_click();

        /// The family member this button draws with now.
        [[nodiscard]] SkinState state() const;

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
        void click();

        int clicks_ = 0;
    };

} // namespace rct3::ui
