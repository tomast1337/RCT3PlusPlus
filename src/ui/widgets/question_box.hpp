// SPDX-License-Identifier: GPL-3.0-or-later
//
// A modal window with a wrapped text and a row of text buttons, centred below it: a question for the user,
// or a notice with one button. While it is shown, nothing below it takes the pointer or the keys (see
// Desktop).
//
// The first button is the default, clicked by Enter, and the last one the cancel, clicked by Escape; the
// close button in the title bar answers as the last one too. The owner asks with take_answer() which
// button was clicked; the box closes with that answer.
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
        Rect button_inset;  ///< Of the buttons' text; see Button::text_inset
        FontRole font = FontRole::Small;
        float width = 320;
        float height = 180;
        float button_width = 70;
        float button_height = 26;
        float button_gap = 6;
        float text_gap = 8; ///< Between the text and the buttons
    };

    class QuestionBox : public Window
    {
    public:
        QuestionBox(std::string name, const QuestionBoxStyle& style, std::u16string title, std::u16string text, const std::vector<std::u16string>& answers);

        [[nodiscard]] Label& text()
        {
            return *text_;
        }
        [[nodiscard]] const std::vector<Button*>& buttons() const
        {
            return buttons_;
        }

        /// The index of the button clicked, once; the box closes with it.
        [[nodiscard]] std::optional<std::size_t> take_answer();

        /// Keeps the title bar's close button from closing the box before take_answer() reads it.
        void update(const UpdateContext& context) override;

    private:
        Label* text_ = nullptr;
        std::vector<Button*> buttons_;
        bool answered_ = false;
    };

} // namespace rct3::ui
