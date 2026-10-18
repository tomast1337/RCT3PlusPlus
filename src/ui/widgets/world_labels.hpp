// SPDX-License-Identifier: GPL-3.0-or-later
//
// Short labels pinned to points of the park on the screen, such as the heights the view menu shows. Its owner
// sets the labels each frame; they are drawn in their order, so the farthest goes first. Covers its parent
// and lets the pointer through.
#pragma once

#include "ui/widget.hpp"

#include <string>
#include <vector>

namespace rct3::ui
{

    struct WorldLabel
    {
        Point at; ///< The middle of the label's bottom edge
        std::u16string text;
    };

    class WorldLabels : public Widget
    {
    public:
        explicit WorldLabels(std::string name);

        std::vector<WorldLabel> labels;
        Color back = hex("#000000a0"); ///< Our own value
        Color text = hex("#ffffff");

        void draw(DrawContext& context) const override;
    };

} // namespace rct3::ui
