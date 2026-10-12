// SPDX-License-Identifier: GPL-3.0-or-later
//
// A skin, a flat colour, or both, stretched over the widget. Lets the pointer through.
#pragma once

#include "ui/widget.hpp"

#include <optional>
#include <string>

namespace rct3::ui
{

    class Panel : public Widget
    {
    public:
        Panel(std::string name, std::string skin);

        std::string skin;
        Color tint = kWhite;
        std::optional<Color> colour; ///< Drawn under the skin

        void draw(DrawContext& context) const override;
    };

} // namespace rct3::ui
