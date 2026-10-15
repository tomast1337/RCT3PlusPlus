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
        constexpr std::string_view kTutorialTitleEnd = "_Title";
        constexpr std::string_view kContinue = "TutorialWindow_Continue";
        constexpr std::string_view kQuit = "TutorialWindow_Quit";

        // The numbers a step stores for its window's place.
        constexpr std::int32_t kStoredTopRight = 1;
        constexpr std::int32_t kStoredBottomRight = 2;

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

    std::u16string tutorial_title(const StringTable& strings, const save::TutorialScript& script)
    {
        return in_language(strings, std::string(kKeyStart) + script.name + std::string(kTutorialTitleEnd), script.title);
    }

    TutorialGuidePlace tutorial_guide_place(std::int32_t stored)
    {
        switch (stored)
        {
            case kStoredTopRight:
                return TutorialGuidePlace::TopRight;
            case kStoredBottomRight:
                return TutorialGuidePlace::BottomRight;
            default:
                return TutorialGuidePlace::Centre;
        }
    }

    TutorialGuideSize tutorial_guide_size(const TutorialGuideStyle& style, float stored_width, float text_height)
    {
        const Rect& in = style.window.content_inset;
        const float page_width = std::max(style.min_page_width, stored_width);
        const float page_height = std::max(style.min_page_height, style.text_top + text_height + style.text_gap + style.button_size.y);
        return { { page_width, page_height }, { std::max(style.min_width, page_width + in.x0 + in.x1), page_height + in.y0 + in.y1 } };
    }

    float tutorial_guide_text_width(const TutorialGuideStyle& style, float page_width)
    {
        return std::max(0.0f, page_width - 2 * style.text_side);
    }

    Placement tutorial_guide_placement(const TutorialGuideStyle& style, TutorialGuidePlace place, Point size)
    {
        const float m = style.screen_margin;
        switch (place)
        {
            case TutorialGuidePlace::TopRight:
                return top_right(m, m, size.x, size.y);
            case TutorialGuidePlace::BottomRight:
                return bottom_right(m, m, size.x, size.y);
            case TutorialGuidePlace::Centre:
                break;
        }
        return centred(size.x, size.y);
    }

    TutorialGuide::TutorialGuide(std::string name, const TutorialGuideStyle& style, const StringTable& strings, save::TutorialScript script)
        : Window(std::move(name), style.window, style.min_width, 0)
        , style_(style)
        , strings_(strings)
        , script_(std::move(script))
    {
        title = tutorial_title(strings, script_);
        icon = style.portrait;
        // The game's guide has no close button; Escape asks to quit instead.
        close_button().visible = false;

        const Rect& line = style.step_line;
        step_line_ = &add<Label>("step", std::u16string { }, style.font);
        step_line_->placement = { { 0, 0, 1, 0 }, { line.x0, line.y0, -line.x1, line.y0 + line.y1 } };
        step_line_->style.colour = style.text_colour;
        step_line_->style.vertical = Align::Centre;

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
        quit_ = text_button("quit", kQuit);
        continue_ = text_button("continue", kContinue);
        quit_->placement = bottom_left(0, 0, style.button_size.x, style.button_size.y);
        continue_->placement = bottom_right(0, 0, style.button_size.x, style.button_size.y);
        default_button = continue_;
        cancel_button = quit_;
        show(0);
    }

    void TutorialGuide::show(std::size_t index)
    {
        step_ = index;
        TutorialStepText t = tutorial_step_text(strings_, script_, index);
        step_line_->text = std::move(t.title);
        text_->text = std::move(t.text);
        continue_->visible = index + 1 < script_.steps.size();
        to_place_ = true;
        fit(0, false);
    }

    void TutorialGuide::fit(float text_height, bool measured)
    {
        const float stored = step_ < script_.steps.size() ? static_cast<float>(script_.steps[step_].window_width) : 0;
        const TutorialGuideSize size = tutorial_guide_size(style_, stored, text_height);
        text_->placement = top_left(style_.text_side, style_.text_top, tutorial_guide_text_width(style_, size.page.x), size.page.y - style_.text_top - style_.button_size.y);
        if (to_place_)
        {
            const TutorialGuidePlace place = step_ < script_.steps.size() ? tutorial_guide_place(script_.steps[step_].window_position) : TutorialGuidePlace::Centre;
            placement = tutorial_guide_placement(style_, place, size.window);
            // Until the text has been measured the size is a guess; the step goes to its place again then.
            to_place_ = !measured;
        }
        else if (size.window.x != size_.x || size.window.y != size_.y)
        {
            // Both edges of each axis hang from the same fraction of the desktop, so this keeps the top-left.
            Rect& o = placement.offsets;
            o.x1 = o.x0 + size.window.x;
            o.y1 = o.y0 + size.window.y;
        }
        size_ = size.window;
    }

    bool TutorialGuide::take_quit()
    {
        return std::exchange(quit_out_, false);
    }

    void TutorialGuide::update(const UpdateContext& context)
    {
        // Not Window::update: the close button is hidden, and here leaving only asks.
        if (continue_->take_click() && step_ + 1 < script_.steps.size())
            show(step_ + 1);
        if (quit_->take_click())
            quit_out_ = true;
        const Font* f = context.fonts != nullptr ? context.fonts->get(style_.font) : nullptr;
        if (f == nullptr)
        {
            fit(0, false);
            return;
        }
        const float stored = step_ < script_.steps.size() ? static_cast<float>(script_.steps[step_].window_width) : 0;
        const float page_width = tutorial_guide_size(style_, stored, 0).page.x;
        const auto lines = break_lines(*f, text_->text, tutorial_guide_text_width(style_, page_width));
        fit(static_cast<float>(lines.size()) * f->line_height(), true);
    }

} // namespace rct3::ui
