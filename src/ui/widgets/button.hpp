// SPDX-License-Identifier: GPL-3.0-or-later
//
// A push button, or a toggle that stays down until clicked again. Drawn with a skin family (see
// SkinLibrary) and an optional icon centred on top at its own size. When the family has no member for the
// current state the base skin is tinted instead, so plain skins still show presses.
//
// A click is a press and release over the button. The owner asks for it with take_click().
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
        void on_release(Point p, bool inside) override;
        void draw(DrawContext& context) const override;

    private:
        int clicks_ = 0;
    };

} // namespace rct3::ui
