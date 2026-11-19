// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tutorials of one game, shown over the front screen while its menu of games is open: a white disc in
// the top right, the tutorials' round pictures on an arc down its left side with their names beside them,
// the picked one's name larger and its description on the right of the disc, and a start button at the
// bottom right. Tutorial one is picked at first. A tutorial whose park is missing cannot be picked.
//
// The menu knows nothing of files: the owner says which tutorials are there and starts the one taken.
#pragma once

#include "core/types.hpp"
#include "ui/screens/front/layout.hpp"
#include "ui/screens/front/screen.hpp"
#include "ui/tutorials.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui::screens::tutorial
{

    namespace tutorial_picker
    {
        // In the front screen's 1024 by 768 design frame, y down (front/layout.hpp). Everything on
        // the disc is drawn round its centre at one scale, so the disc stays round on any screen.

        /// The disc's radius, and the box it must fit in: right of the arc, clear of the top edge and above
        /// the badges and the bottom strip. The box stretches with the screen; the disc is as large as fits
        /// in it, as near the buttons' centre as it can be while staying inside.
        inline constexpr f32 kDiscRadius = 320;
        inline constexpr Rect kDiscArea { 380, 8, 1016, 590 };

        /// Where the disc lands on a screen: its middle, and screen units per design unit.
        struct DiscPlace
        {
            Point centre;
            f32 scale = 1;

            /// A design point, measured from the buttons' centre, on the screen.
            [[nodiscard]] Point at(Point design) const
            {
                return { centre.x + (design.x - front::front_screen::kRingCentre.x) * scale, centre.y + (design.y - front::front_screen::kRingCentre.y) * scale };
            }
            /// A design rectangle on the screen, at the disc's scale.
            [[nodiscard]] Rect rect(const Rect& design) const
            {
                const Point a = at({ design.x0, design.y0 });
                const Point b = at({ design.x1, design.y1 });
                return { a.x, a.y, b.x, b.y };
            }
        };
        [[nodiscard]] DiscPlace disc_place(const front::front_screen::Frame& frame);

        /// The tutorials on a circle round the buttons' centre, the first at the top.
        inline constexpr f32 kRadius = 270;
        inline constexpr f32 kFirstAngle = 2.35f;
        inline constexpr f32 kAngleStep = 0.26f;
        inline constexpr f32 kButtonSize = 64;
        inline constexpr f32 kStartAngle = 5.6f; ///< The start button, on the same circle

        /// A name runs from kLabelStart buttons right of its button's left edge to kLabelReach right of
        /// its right edge, as tall as the button.
        inline constexpr f32 kLabelStart = 1.45f;
        inline constexpr f32 kLabelReach = 280;
        inline constexpr Rect kDescription { 780, 96, 980, 396 };

        [[nodiscard]] f32 angle(usize index);
    } // namespace tutorial_picker

    struct TutorialPickerStyle
    {
        std::string button;
        std::string start_icon;
        /// The disc's quarters round the buttons' centre: top left, top right, bottom left, bottom right.
        std::array<std::string, 4> quarters;
        Color text_colour = hex("#000000");
        bool round = false; ///< The front screen's design at one scale, as the screen under it is laid out
    };

    class TutorialPicker : public Widget
    {
    public:
        TutorialPicker(std::string name, TutorialPickerStyle style, const StringTable& strings, std::vector<TutorialInfo> tutorials);

        [[nodiscard]] usize size() const
        {
            return tutorials_.size();
        }
        [[nodiscard]] const TutorialInfo& tutorial(usize index) const
        {
            return tutorials_.at(index);
        }
        [[nodiscard]] usize picked() const
        {
            return picked_;
        }
        /// Picks tutorial `index`, if it is there.
        void pick(usize index);
        /// Whether tutorial `index`'s park is there; all are at first.
        void set_available(usize index, bool available);

        /// The tutorial to start, once each time start is pressed.
        [[nodiscard]] std::optional<usize> take_start();

        /// The two lines for the bottom strip while the pointer is over the menu: a tutorial's name, or what
        /// the start button does.
        [[nodiscard]] std::optional<std::array<std::u16string, 2>> help() const;

        // For tests and owners that drive the menu.
        [[nodiscard]] front::FrontButton& button(usize index) const
        {
            return *buttons_.at(index);
        }
        [[nodiscard]] front::FrontButton& start_button() const
        {
            return *start_;
        }

        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        TutorialPickerStyle style_;
        const StringTable& strings_;
        std::vector<TutorialInfo> tutorials_;
        std::vector<front::FrontButton*> buttons_;
        std::vector<Rect> labels_;
        front::FrontButton* start_ = nullptr;
        usize picked_ = 0;
        std::optional<usize> start_out_;
    };

} // namespace rct3::ui::screens::tutorial
