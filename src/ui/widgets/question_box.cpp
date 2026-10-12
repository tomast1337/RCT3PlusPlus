// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/question_box.hpp"

namespace rct3::ui
{

    QuestionBox::QuestionBox(std::string name, const QuestionBoxStyle& style, std::u16string title, std::u16string text, const std::vector<std::u16string>& answers)
        : Window(std::move(name), style.window, style.width, style.height)
    {
        modal = true;
        this->title = std::move(title);

        const float bottom = answers.empty() ? 0 : style.button_height + style.text_gap;
        text_ = &content().add<Label>("text", std::move(text), style.font);
        text_->placement = { { 0, 0, 1, 1 }, { 0, 0, 0, -bottom } };
        text_->style.wrap = true;

        const auto n = static_cast<float>(answers.size());
        const float row = n * style.button_width + (n > 0 ? (n - 1) * style.button_gap : 0);
        for (std::size_t i = 0; i < answers.size(); ++i)
        {
            Button& b = content().add<Button>("answer" + std::to_string(i), style.button);
            b.text = answers[i];
            b.font = style.font;
            b.text_inset = style.button_inset;
            const float x = -row / 2 + static_cast<float>(i) * (style.button_width + style.button_gap);
            b.placement = { { 0.5f, 1, 0.5f, 1 }, { x, -style.button_height, x + style.button_width, 0 } };
            buttons_.push_back(&b);
        }
        if (!buttons_.empty())
        {
            default_button = buttons_.front();
            cancel_button = buttons_.back();
        }
    }

    std::optional<std::size_t> QuestionBox::take_answer()
    {
        if (answered_)
            return std::nullopt;
        std::optional<std::size_t> answer;
        for (std::size_t i = 0; i < buttons_.size() && !answer; ++i)
        {
            if (buttons_[i]->take_click())
                answer = i;
        }
        if (!answer && !buttons_.empty() && close_button().take_click())
            answer = buttons_.size() - 1;
        if (answer)
        {
            answered_ = true;
            close();
        }
        return answer;
    }

    void QuestionBox::update(const UpdateContext& context)
    {
        // Window::update would close the box on its close button; take_answer() reads that click instead.
        if (buttons_.empty())
            Window::update(context);
    }

} // namespace rct3::ui
