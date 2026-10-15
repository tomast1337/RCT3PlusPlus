// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small board of colour swatches to pick one from, as opened by a paint button. The swatches stand in a
// grid of rows reading left to right, the first at the top left, each a round button of the colour's own
// picture on a ring; the colour picked before has a ring of its own so it can be told. The board's size
// follows from the count, the columns and the pitch; its owner places it.
//
// A pick is a click on a swatch; the owner asks for it with take_pick().
#pragma once

#include "ui/widgets/button.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct ColourPaletteStyle
    {
        std::string back;            ///< Behind the swatches, stretched in nine parts
        std::string swatch_prefix;   ///< A swatch's picture is this followed by the colour's number
        std::string ring;            ///< Behind each swatch
        std::string current_ring;    ///< Behind the colour picked before
        float swatch = 16;           ///< A swatch's size
        float pitch = 18;            ///< From one swatch to the next, across and down
        float picture_scale = 0.75f; ///< Of the colour's picture, of its own size
        float swell = 4;             ///< Units a swatch grows by on each side under the pointer
        float margin = 9;            ///< From the board's edge to the swatches
    };

    class ColourPalette : public Widget
    {
    public:
        /// `count` colours in rows of `columns`, `current` marked.
        ColourPalette(std::string name, ColourPaletteStyle style, std::size_t count, std::size_t columns, std::size_t current);

        /// The board's size for `count` colours in rows of `columns`.
        [[nodiscard]] static Point size(const ColourPaletteStyle& style, std::size_t count, std::size_t columns);

        /// The colour the player picked since the last call.
        [[nodiscard]] std::optional<std::size_t> take_pick();
        [[nodiscard]] std::size_t current() const
        {
            return current_;
        }
        [[nodiscard]] Button& swatch(std::size_t i) const
        {
            return *swatches_.at(i);
        }
        [[nodiscard]] std::size_t count() const
        {
            return swatches_.size();
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }

        void draw(DrawContext& context) const override;

    private:
        ColourPaletteStyle style_;
        std::vector<Button*> swatches_;
        std::size_t current_ = 0;
    };

} // namespace rct3::ui
