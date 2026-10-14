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
        // In the front screen's 1024 by 768 design frame, y down (front_screen_layout.hpp).

        /// The tutorials on a circle round the buttons' centre, the first at the top.
        inline constexpr float kRadius = 275;
        inline constexpr float kFirstAngle = 2.4f;
        inline constexpr float kAngleStep = 0.25f;
        inline constexpr float kButtonSize = 64;
        inline constexpr float kStartAngle = 5.65f; ///< The start button, on the same circle

        /// A name runs from kLabelStart buttons right of its button's left edge to kLabelReach right of
        /// its right edge, as tall as the button.
        inline constexpr float kLabelStart = 1.5f;
        inline constexpr float kLabelReach = 270;
        inline constexpr Rect kDescription { 784, 91, 984, 391 };

        [[nodiscard]] float angle(std::size_t index);
    } // namespace tutorial_picker

    struct TutorialPickerStyle
    {
        std::string button;
        std::string start_icon;
        /// The disc's quarters round the buttons' centre: top left, top right, bottom left, bottom right.
        std::array<std::string, 4> quarters;
        Color text_colour = hex("#000000");
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
