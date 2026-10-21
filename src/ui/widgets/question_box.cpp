// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/question_box.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
{

    QuestionBox::QuestionBox(std::string name, const QuestionBoxStyle& style, std::u16string text, const std::vector<QuestionAnswer>& answers)
        : Window(std::move(name), style.window, style.min_width, 0)
        , style_(style)
    {
        modal = true;
        placement = centred(style.min_width, 0);

        text_ = &content().add<Label>("text", std::move(text), style.font);
        text_->style.wrap = true;
        text_->style.colour = style.text_colour;

        hover_ = &content().add<Label>("hover", std::u16string { }, style.font);
        hover_->style.colour = style.hover_colour;
        hover_->style.vertical = Align::Centre;

        for (std::size_t i = 0; i < answers.size(); ++i)
        {
            const QuestionAnswer& a = answers[i];
            Button& b = content().add<Button>(a.icon.empty() ? std::format("answer{}", i) : a.icon, style.button, a.icon);
            b.font = style.font;
            if (a.icon.empty())
            {
                b.text = a.name;
                b.text_inset = style.button_inset;
            }
            else
                b.tooltip = a.name;
            buttons_.push_back(&b);
            names_.push_back(a.name);
        }
        if (!buttons_.empty())
        {
            default_button = buttons_.front();
            cancel_button = buttons_.back();
        }
        fit(0);
    }

    void QuestionBox::fit(float text_height)
    {
        const float m = style_.margin;
        const Rect& in = style_.window.content_inset;
        float row = 0;
        for (const Button* b : buttons_)
            row += (b->icon.empty() ? style_.text_button_width : style_.button_size) + (row > 0 ? style_.button_gap : 0);
        row_width_ = row;
        const float content_width = std::max(style_.min_width - in.x0 - in.x1, m + std::max(style_.text_width, style_.hover_width + style_.button_gap + row) + m);
        const float buttons_top = m + text_height + (text_height > 0 ? m : 0);
        const float content_height = buttons_top + (buttons_.empty() ? 0 : style_.button_size + m);

        text_->placement = top_left(m, m, content_width - 2 * m, text_height);
        hover_->placement = top_left(m, buttons_top, style_.hover_width, style_.button_size);
        float x = content_width - m - row;
        for (Button* b : buttons_)
        {
            const float w = b->icon.empty() ? style_.text_button_width : style_.button_size;
            b->placement = top_left(x, buttons_top, w, style_.button_size);
            x += w + style_.button_gap;
        }
        const float width = content_width + in.x0 + in.x1;
        const float height = content_height + in.y0 + in.y1;
        if (placement.offsets.x1 - placement.offsets.x0 != width || placement.offsets.y1 - placement.offsets.y0 != height)
            placement = centred(width, height);
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

        if (const auto f = context.fonts ? context.fonts->get(style_.font) : std::nullopt)
        {
            const auto lines = break_lines(*f, text_->text, style_.text_width);
            fit(static_cast<float>(lines.size()) * f->line_height());
        }
        hover_->text.clear();
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->hovered() && buttons_[i]->text.empty())
                hover_->text = names_[i];
        }
    }

} // namespace rct3::ui
