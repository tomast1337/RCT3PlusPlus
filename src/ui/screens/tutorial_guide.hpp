// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that leads the player through a tutorial, one step at a time: the step's title in the title
// bar, its text, and a continue button that moves to the next step (none on the last) beside a quit button.
// The text is the language's where the strings have it, under keys built from the tutorial's name and the
// step's number, else the English stored in the park. The window does not stop the player using the park
// round it.
//
// Quit and the close button only ask: the owner confirms and leaves the tutorial.
#pragma once

#include "save/model/tutorial.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <string>

namespace rct3::ui
{

    /// The title and text of step `index` (from 0) of `script`, in the language where the strings have it.
    struct TutorialStepText
    {
        std::u16string title;
        std::u16string text;
    };
    [[nodiscard]] TutorialStepText tutorial_step_text(const StringTable& strings, const save::TutorialScript& script, std::size_t index);

    namespace tutorial_guide
    {
        inline constexpr float kMargin = 16; ///< From the desktop's top-right corner
    }

    class TutorialGuide : public Window
    {
    public:
        /// Shows step one of `script`. Its look is the game's message boxes'.
        TutorialGuide(std::string name, const QuestionBoxStyle& style, const StringTable& strings, save::TutorialScript script);

        [[nodiscard]] std::size_t step() const
        {
            return step_;
        }
        [[nodiscard]] std::size_t steps() const
        {
            return script_.steps.size();
        }
        [[nodiscard]] const save::TutorialScript& script() const
        {
            return script_;
        }
        [[nodiscard]] Label& text() const
        {
            return *text_;
        }
        [[nodiscard]] Button& continue_button() const
        {
            return *continue_;
        }
        [[nodiscard]] Button& quit_button() const
        {
            return *quit_;
        }

        /// True once each time the player asks to leave.
        [[nodiscard]] bool take_quit();

        void update(const UpdateContext& context) override;

    private:
        void show(std::size_t index);
        void fit(float text_height);

        QuestionBoxStyle style_;
        const StringTable& strings_;
        save::TutorialScript script_;
        Label* text_ = nullptr;
        Button* continue_ = nullptr;
        Button* quit_ = nullptr;
        std::size_t step_ = 0;
        float width_ = 0;
        float height_ = 0;
        bool quit_out_ = false;
    };

} // namespace rct3::ui
