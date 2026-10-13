// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_end.hpp"

#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kScreenName = "front";
        constexpr std::string_view kQuitTitle = "FrontScreen_Title_ConfirmExit";
        constexpr std::string_view kQuitText = "FrontScreen_Message_ConfirmExit";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::size_t kYesAnswer = 0;

    } // namespace

    FrontEnd::FrontEnd(Desktop& desktop, FrontEndStyle style, const StringTable& strings, config::GameOptions& options, FrontEndSetup setup)
        : desktop_(desktop)
        , style_(std::move(style))
        , strings_(strings)
        , options_(options)
        , setup_(std::move(setup))
    {
        screen_ = &desktop_.root().add<FrontScreen>(std::string(kScreenName), strings_, setup_.expansions, setup_.choices.language);
        if (setup_.settled)
            screen_->settle();
        if (setup_.open_options)
            open_options();
    }

    OptionsDialog* FrontEnd::options_window() const
    {
        return static_cast<OptionsDialog*>(desktop_.root().find(kOptionsName));
    }

    void FrontEnd::open_options()
    {
        if (OptionsDialog* open = options_window())
        {
            desktop_.root().raise(*open);
            return;
        }
        desktop_.root().add<OptionsDialog>(std::string(kOptionsName), style_.options, strings_, options_, setup_.choices);
        options_open_ = true;
    }

    void FrontEnd::update()
    {
        while (const auto request = screen_->take_request())
        {
            switch (request->action)
            {
                case FrontAction::Options:
                    open_options();
                    break;
                case FrontAction::Quit:
                    if (question_ == nullptr)
                        question_ = &desktop_.root().add<QuestionBox>(std::string(kQuestionName), style_.question, std::u16string(strings_.get(kQuitTitle)), std::u16string(strings_.get(kQuitText)), std::vector<std::u16string> { std::u16string(strings_.get(kYes)), std::u16string(strings_.get(kNo)) });
                    break;
                default:
                    unhandled_.push_back(*request);
                    break;
            }
        }
        if (question_ != nullptr)
        {
            if (const auto answer = question_->take_answer())
            {
                quit_ = quit_ || *answer == kYesAnswer;
                question_ = nullptr; // It closes with the answer
            }
        }

        OptionsDialog* dialog = options_window();
        if (dialog != nullptr)
        {
            if (auto language = dialog->take_language())
                language_ = std::move(language);
            if (const auto size = dialog->take_screen_size())
                screen_size_ = size;
            if (const auto keys = dialog->take_developer_keys())
                developer_keys_ = keys;
        }
        else if (options_open_)
        {
            options_open_ = false;
            options_closed_ = true;
        }
    }

    bool FrontEnd::take_quit()
    {
        return std::exchange(quit_, false);
    }

    bool FrontEnd::take_options_closed()
    {
        return std::exchange(options_closed_, false);
    }

    std::optional<std::string> FrontEnd::take_language()
    {
        return std::exchange(language_, std::nullopt);
    }

    std::optional<std::size_t> FrontEnd::take_screen_size()
    {
        return std::exchange(screen_size_, std::nullopt);
    }

    std::optional<KeyCombo> FrontEnd::take_developer_keys()
    {
        return std::exchange(developer_keys_, std::nullopt);
    }

    bool FrontEnd::waiting_for_keys() const
    {
        const OptionsDialog* dialog = options_window();
        return dialog != nullptr && dialog->waiting_for_keys();
    }

    std::optional<FrontRequest> FrontEnd::take_unhandled()
    {
        if (unhandled_.empty())
            return std::nullopt;
        const FrontRequest r = unhandled_.front();
        unhandled_.erase(unhandled_.begin());
        return r;
    }

} // namespace rct3::ui
