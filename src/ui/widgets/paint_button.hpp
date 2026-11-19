// SPDX-License-Identifier: GPL-3.0-or-later
//
// A button that shows one, two or three paint pots, each with a blob of the colour it holds. The pot under
// the pointer lights up, and a click tells which pot it was on, so the owner can open colours for that one.
// The owner can keep a pot lit while it picks a colour for it.
#pragma once

#include "core/types.hpp"
#include "ui/widgets/button.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui::widgets
{

    /// Where a pot's blob of colour sits on the button's icon, in the icon's own units with y down.
    struct PaintSpot
    {
        Point centre;
        f32 size = 0;
    };

    inline constexpr usize kMaxPots = 3;

    struct PaintButtonStyle
    {
        std::string face;                                            ///< Behind the icon
        std::array<std::string, kMaxPots> pots;                      ///< Icon for one, two and three pots
        std::array<std::array<std::string, kMaxPots>, kMaxPots> lit; ///< The same with pot k lit
        std::array<std::vector<PaintSpot>, kMaxPots> spots;          ///< Of each pot, for one, two and three
        std::string paint_prefix;                                    ///< A blob's picture is this followed by the colour's number
        f32 icon_size = 40;                                          ///< Of the icons' own pictures
    };

    class PaintButton : public Button
    {
    public:
        /// Pots holding `colours`, one to three of them.
        PaintButton(std::string name, PaintButtonStyle style, std::vector<usize> colours);

        std::vector<usize> colours;
        std::optional<usize> held; ///< A pot kept lit, such as the one colours are being picked for

        /// The pot the last click was on, once for each click since the last call.
        [[nodiscard]] std::optional<usize> take_pot();
        /// The pot nearest `p`, a point on the screen.
        [[nodiscard]] usize pot_at(Point p) const;
        /// The pot lit now, if any.
        [[nodiscard]] std::optional<usize> lit() const;

        void on_hover(Point p) override;
        void on_press(Point p) override;
        void on_release(Point p, bool inside) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        /// Where pot `i`'s blob is drawn, as the face is now.
        [[nodiscard]] Rect spot(usize i) const;

        PaintButtonStyle style_;
        std::optional<usize> under_;
        std::optional<usize> pressed_on_; // The pot the pointer went down on
        std::optional<usize> clicked_on_; // And of the last click
    };

} // namespace rct3::ui::widgets
