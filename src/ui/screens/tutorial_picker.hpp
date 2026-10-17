// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tutorials of one game, shown over the front screen while its menu of games is open: a white disc in
// the top right, the tutorials' round pictures on an arc down its left side with their names beside them,
// the picked one's name larger and its description on the right of the disc, and a start button at the
// bottom right. Tutorial one is picked at first. A tutorial whose park is missing cannot be picked.
//
// The menu knows nothing of files: the owner says which tutorials are there and starts the one taken.
#pragma once

#include "ui/screens/front_screen.hpp"
#include "ui/screens/front_screen_layout.hpp"
#include "ui/tutorials.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    namespace tutorial_picker
    {
        // In the front screen's 1024 by 768 design frame, y down (front_screen_layout.hpp). Everything on
        // the disc is drawn round its centre at one scale, so the disc stays round on any screen.

        /// The disc's radius, and the box it must fit in: right of the arc, clear of the top edge and above
        /// the badges and the bottom strip. The box stretches with the screen; the disc is as large as fits
        /// in it, as near the buttons' centre as it can be while staying inside.
        inline constexpr float kDiscRadius = 320; ///< Our own values from here on
        inline constexpr Rect kDiscArea { 380, 8, 1016, 590 };

        /// Where the disc lands on a screen: its middle, and screen units per design unit.
        struct DiscPlace
        {
            Point centre;
            float scale = 1;

            /// A design point, measured from the buttons' centre, on the screen.
            [[nodiscard]] Point at(Point design) const
            {
                return { centre.x + (design.x - front_screen::kRingCentre.x) * scale, centre.y + (design.y - front_screen::kRingCentre.y) * scale };
            }
            /// A design rectangle on the screen, at the disc's scale.
            [[nodiscard]] Rect rect(const Rect& design) const
            {
                const Point a = at({ design.x0, design.y0 });
                const Point b = at({ design.x1, design.y1 });
                return { a.x, a.y, b.x, b.y };
            }
        };
        [[nodiscard]] DiscPlace disc_place(const front_screen::Frame& frame);

        /// The tutorials on a circle round the buttons' centre, the first at the top.
        inline constexpr float kRadius = 270;
        inline constexpr float kFirstAngle = 2.35f;
        inline constexpr float kAngleStep = 0.26f;
        inline constexpr float kButtonSize = 64;
        inline constexpr float kStartAngle = 5.6f; ///< The start button, on the same circle

        /// A name runs from kLabelStart buttons right of its button's left edge to kLabelReach right of
        /// its right edge, as tall as the button.
        inline constexpr float kLabelStart = 1.45f;
        inline constexpr float kLabelReach = 280;
        inline constexpr Rect kDescription { 780, 96, 980, 396 };

        [[nodiscard]] float angle(std::size_t index);
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

        [[nodiscard]] std::size_t size() const
        {
            return tutorials_.size();
        }
        [[nodiscard]] const TutorialInfo& tutorial(std::size_t index) const
        {
            return tutorials_.at(index);
        }
        [[nodiscard]] std::size_t picked() const
        {
            return picked_;
        }
        /// Picks tutorial `index`, if it is there.
        void pick(std::size_t index);
        /// Whether tutorial `index`'s park is there; all are at first.
        void set_available(std::size_t index, bool available);

        /// The tutorial to start, once each time start is pressed.
        [[nodiscard]] std::optional<std::size_t> take_start();

        /// The two lines for the bottom strip while the pointer is over the menu: a tutorial's name, or what
        /// the start button does.
        [[nodiscard]] std::optional<std::array<std::u16string, 2>> help() const;

        // For tests and owners that drive the menu.
        [[nodiscard]] FrontButton& button(std::size_t index) const
        {
            return *buttons_.at(index);
        }
        [[nodiscard]] FrontButton& start_button() const
        {
            return *start_;
        }

        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        TutorialPickerStyle style_;
        const StringTable& strings_;
        std::vector<TutorialInfo> tutorials_;
        std::vector<FrontButton*> buttons_;
        std::vector<Rect> labels_;
        FrontButton* start_ = nullptr;
        std::size_t picked_ = 0;
        std::optional<std::size_t> start_out_;
    };

} // namespace rct3::ui
