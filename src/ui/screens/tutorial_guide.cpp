// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial_guide.hpp"

#include "strings/unicode.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kKeyStart = "TutorialText_";
        constexpr std::string_view kStep = "_Step";
        constexpr std::string_view kTitleEnd = "_StepTitle";
        constexpr std::string_view kTextEnd = "_Text";
        constexpr std::string_view kContinue = "TutorialWindow_Continue";
        constexpr std::string_view kQuit = "TutorialWindow_Quit";

        std::u16string in_language(const StringTable& strings, const std::string& key, const std::string& stored)
        {
            const std::u16string_view text = strings.get(key);
            return text.empty() ? strings::to_utf16(stored) : std::u16string(text);
        }
    } // namespace

    TutorialStepText tutorial_step_text(const StringTable& strings, const save::TutorialScript& script, std::size_t index)
    {
        if (index >= script.steps.size())
            return { };
        const save::TutorialStep& step = script.steps[index];
        const std::string key = std::string(kKeyStart) + script.name + std::string(kStep) + std::to_string(index + 1);
        return { in_language(strings, key + std::string(kTitleEnd), step.title), in_language(strings, key + std::string(kTextEnd), step.text) };
    }

    TutorialGuide::TutorialGuide(std::string name, const QuestionBoxStyle& style, const StringTable& strings, save::TutorialScript script)
        : Window(std::move(name), style.window, style.min_width, 0)
        , style_(style)
        , strings_(strings)
        , script_(std::move(script))
    {
        text_ = &content().add<Label>("text", std::u16string { }, style.font);
        text_->style.wrap = true;
        text_->style.colour = style.text_colour;
        const auto text_button = [&](std::string_view widget, std::string_view key) {
            Button& b = content().add<Button>(std::string(widget), style.button);
            b.font = style.font;
            b.text = strings.get(key);
            b.text_inset = style.button_inset;
            return &b;
        };
        continue_ = text_button("continue", kContinue);
        quit_ = text_button("quit", kQuit);
        default_button = continue_;
        show(0);
    }

    void TutorialGuide::show(std::size_t index)
    {
        step_ = index;
        TutorialStepText t = tutorial_step_text(strings_, script_, index);
        title = std::move(t.title);
        text_->text = std::move(t.text);
        continue_->visible = index + 1 < script_.steps.size();
        const float stored = index < script_.steps.size() ? static_cast<float>(script_.steps[index].window_width) : 0;
        width_ = std::max(style_.min_width, stored);
        fit(0);
    }

    void TutorialGuide::fit(float text_height)
    {
        const float m = style_.margin;
        const Rect& in = style_.window.content_inset;
        const float content_width = width_ - in.x0 - in.x1;
        const float buttons_top = m + text_height + m;
        text_->placement = top_left(m, m, content_width - 2 * m, text_height);
        float x = content_width - m;
        for (Button* b : { quit_, continue_ })
        {
            if (!b->visible)
                continue;
            x -= style_.text_button_width;
            b->placement = top_left(x, buttons_top, style_.text_button_width, style_.button_size);
            x -= style_.button_gap;
        }
        const float height = buttons_top + style_.button_size + m + in.y0 + in.y1;
        // A new size starts in the top-right corner; otherwise the window stays where the player put it.
        if (height != height_ || placement.offsets.x1 - placement.offsets.x0 != width_)
            placement = top_right(tutorial_guide::kMargin, tutorial_guide::kMargin, width_, height);
        height_ = height;
    }

    bool TutorialGuide::take_quit()
    {
        return std::exchange(quit_out_, false);
    }

    void TutorialGuide::update(const UpdateContext& context)
    {
        // Not Window::update: its close button would close the window, and here it only asks to quit.
        if (continue_->take_click() && step_ + 1 < script_.steps.size())
            show(step_ + 1);
        if (quit_->take_click() || close_button().take_click())
            quit_out_ = true;
        if (const Font* f = context.fonts != nullptr ? context.fonts->get(style_.font) : nullptr)
        {
            const Rect& in = style_.window.content_inset;
            const float wrap = width_ - in.x0 - in.x1 - 2 * style_.margin;
            fit(static_cast<float>(break_lines(*f, text_->text, wrap).size()) * f->line_height());
        }
    }

} // namespace rct3::ui
