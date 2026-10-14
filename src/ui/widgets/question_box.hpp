// SPDX-License-Identifier: GPL-3.0-or-later
//
// A modal window asking the user something, laid out as the game's small message boxes: the text at the top,
// wrapped, and the answers as a row of round buttons in the bottom-right corner, each with an icon (a tick to
// go ahead, a cross to back out). The name of the answer under the pointer shows on the left of the row. An
// answer without an icon is a wider button with its name on it instead. While the box is shown, nothing below
// it takes the pointer or the keys (see Desktop).
//
// The box is as wide as the text allows and as tall as the text needs, and opens in the middle of the screen.
// The first answer is the default, clicked by Enter, and the last one the cancel, clicked by Escape; the close
// button answers as the last one too. The owner asks with take_answer() which answer was given; the box
// closes with it.
#pragma once

#include "ui/widgets/label.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct QuestionBoxStyle
    {
        WindowStyle window;
        std::string button; ///< Skin family of the answer buttons
        Rect button_inset;  ///< Of the text of answers without an icon; see Button::text_inset
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color hover_colour = hex("#2484be"); ///< Of the hovered answer's name
        float min_width = 282;               ///< Of the window
        float text_width = 239;              ///< Where the text wraps
        float margin = 8;                    ///< Around the text and the buttons, inside the content
        float button_size = 30;
        float text_button_width = 70; ///< Of answers without an icon
        float button_gap = 8;
        float hover_width = 128;
        std::string confirm_icon; ///< For owners: the icons of answers that go ahead and that back out
        std::string cancel_icon;
    };

    struct QuestionAnswer
    {
        std::u16string name;
        std::string icon; ///< Skin; none for a button with the name on it
    };

    class QuestionBox : public Window
    {
    public:
        QuestionBox(std::string name, const QuestionBoxStyle& style, std::u16string text, const std::vector<QuestionAnswer>& answers);

        [[nodiscard]] Label& text()
        {
            return *text_;
        }
        [[nodiscard]] const std::vector<Button*>& buttons() const
        {
            return buttons_;
        }
        /// The name of the answer under the pointer.
        [[nodiscard]] const Label& hover() const
        {
            return *hover_;
        }

        /// The index of the answer given, once; the box closes with it.
        [[nodiscard]] std::optional<std::size_t> take_answer();

        /// Fits the box to its text, shows the hovered answer's name, and keeps the close button from closing
        /// the box before take_answer() reads it.
        void update(const UpdateContext& context) override;

    private:
        void fit(float text_height);

        QuestionBoxStyle style_;
        Label* text_ = nullptr;
        Label* hover_ = nullptr;
        std::vector<Button*> buttons_;
        std::vector<std::u16string> names_;
        float row_width_ = 0;
        bool answered_ = false;
    };

} // namespace rct3::ui
