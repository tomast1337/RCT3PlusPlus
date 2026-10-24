// SPDX-License-Identifier: GPL-3.0-or-later
//
// A push button, or a toggle that stays down until clicked again. Drawn with a skin family (see
// SkinLibrary), with an optional icon centred on top at its own size and optional text centred over that.
// When the family has no member for the current state the base skin is tinted instead, so plain skins still
// show presses. Text is black, grey when the button is disabled, as in the game.
//
// A click is a press and release over the button, or Space or Enter while it has the keyboard focus. The
// owner asks for it with take_click().
//
// A button can swell while the pointer is over it: its face grows by a few units on each side, eased in
// and out, while the part that takes the pointer stays put.
#pragma once

#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{

    class Button : public Widget
    {
    public:
        Button(std::string name, std::string skin, std::string icon = { });

        std::string skin;                   ///< Family base name
        std::string icon;                   ///< Optional skin drawn on top
        float icon_scale = 1;               ///< Of the icon's own size
        Color icon_colour = hex("#ffffff"); ///< Multiplies the icon while enabled
        std::u16string text;
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color disabled_text_colour = hex("#7f7f7f");
        Rect text_inset; ///< Taken off each side before centring the text, e.g. to leave out a skin's shadow
        bool toggle = false;
        bool on = false; ///< Toggles only
        /// Clicking does nothing now, though the face, whose picture shows it, stays as it is: the pointer says so.
        bool inert = false;
        float swell = 0;         ///< Units the face grows by on each side while the pointer is over it
        float swell_time = 0.1f; ///< Seconds to grow or shrink fully

        /// True once for each click since the last call.
        [[nodiscard]] bool take_click();

        /// As if the user clicked: counts a click and turns a toggle over.
        void click();

        /// The family member this button draws with now.
        [[nodiscard]] SkinState state() const;
        /// Where the face is drawn: the button's rectangle, grown while it swells.
        [[nodiscard]] Rect face() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool clicks() const override
        {
            return true;
        }
        [[nodiscard]] CursorKind cursor(Point) const override
        {
            return inert ? CursorKind::NotAllowed : CursorKind::Hand;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_release(Point p, bool inside) override;
        /// Space and Enter work like a click.
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        int clicks_ = 0;
        float grown_ = 0; // 0 to 1, how far into its swell
    };

} // namespace rct3::ui
