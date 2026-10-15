// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_end.hpp"

#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kScreenName = "front";
        constexpr std::string_view kQuitText = "FrontScreen_Message_ConfirmExit";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::size_t kYesAnswer = 0;

    } // namespace

    FrontEnd::FrontEnd(Desktop& desktop, FrontEndStyle style, const StringTable& strings, config::GameOptions& options, FrontEndSetup setup)
        : desktop_(desktop)
        , style_(std::move(style))
        , strings_(strings)
        , setup_(std::move(setup))
        , options_(desktop, style_.options, strings, options, setup_.choices)
    {
        screen_ = &desktop_.root().add<FrontScreen>(std::string(kScreenName), strings_, setup_.expansions, setup_.choices.language);
        // The career map and the tutorials stand on the front screen's artwork: laid out the same way.
        style_.map.round = style_.round;
        style_.tutorials.round = style_.round;
        screen_->round = style_.round;
        if (setup_.settled)
            screen_->settle();
        if (setup_.open_options)
            open_options();
    }

    OptionsDialog* FrontEnd::options_window() const
    {
        return options_.window();
    }

    void FrontEnd::open_options()
    {
        options_.open();
    }

    FileDialog* FrontEnd::file_dialog() const
    {
        return static_cast<FileDialog*>(desktop_.root().find(kFilesName));
    }

    void FrontEnd::open_files(FrontAction action, config::UserFolder folder)
    {
        if (FileDialog* open = file_dialog())
        {
            if (folder == file_folder_)
            {
                desktop_.root().raise(*open);
                return;
            }
            desktop_.root().remove(*open);
        }
        file_folder_ = folder;
        file_action_ = action;
        ++file_dialogs_opened_;
        desktop_.root().add<FileDialog>(std::string(kFilesName), style_.files, strings_, FileDialogMode::Load, setup_.twelve_hour);
    }

    CareerDialog* FrontEnd::career_dialog() const
    {
        return static_cast<CareerDialog*>(desktop_.root().find(kCareersName));
    }

    void FrontEnd::open_careers(int pack)
    {
        if (CareerDialog* open = career_dialog())
        {
            if (pack == career_pack_)
            {
                desktop_.root().raise(*open);
                return;
            }
            desktop_.root().remove(*open);
        }
        career_pack_ = pack;
        ++career_dialogs_opened_;
        desktop_.root().add<CareerDialog>(std::string(kCareersName), style_.careers, strings_);
    }

    CareerBoard& FrontEnd::open_career_board(std::vector<CareerBoardScenario> scenarios, std::size_t picked)
    {
        if (CareerDialog* dialog = career_dialog())
            desktop_.root().remove(*dialog);
        if (CareerBoard* old = career_board())
            desktop_.root().remove(*old);
        screen_->visible = false;
        ++career_boards_opened_;
        auto& map = desktop_.root().add<CareerBoard>(std::string(kCareerBoardName), style_.map, strings_, std::move(scenarios), picked);
        // Under the windows already open, as the front screen was.
        desktop_.root().lower(map);
        return map;
    }

    CareerBoard* FrontEnd::career_board() const
    {
        return static_cast<CareerBoard*>(desktop_.root().find(kCareerBoardName));
    }

    TutorialPicker* FrontEnd::tutorial_picker() const
    {
        return static_cast<TutorialPicker*>(desktop_.root().find(kTutorialsName));
    }

    void FrontEnd::open_tutorials(int pack)
    {
        if (TutorialPicker* open = tutorial_picker())
        {
            if (pack == tutorial_pack_)
                return;
            desktop_.root().remove(*open);
        }
        tutorial_pack_ = pack;
        ++tutorial_pickers_opened_;
        auto& menu = desktop_.root().add<TutorialPicker>(std::string(kTutorialsName), style_.tutorials, strings_, tutorials(strings_, pack, setup_.expansions));
        // Part of the screen, under any window open over it.
        desktop_.root().lower(menu);
        desktop_.root().lower(*screen_);
    }

    void FrontEnd::ask_quit()
    {
        if (question_ == nullptr)
            question_ = &desktop_.root().add<QuestionBox>(std::string(kQuestionName), style_.question, std::u16string(strings_.get(kQuitText)), std::vector<QuestionAnswer> { { std::u16string(strings_.get(kYes)), style_.question.confirm_icon }, { std::u16string(strings_.get(kNo)), style_.question.cancel_icon } });
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
                case FrontAction::Campaign:
                    open_careers(request->pack);
                    break;
                case FrontAction::Tutorial:
                    open_tutorials(request->pack);
                    break;
                case FrontAction::LoadSandbox:
                    open_files(request->action, config::UserFolder::Parks);
                    break;
                case FrontAction::LoadCustomScenario:
                    open_files(request->action, config::UserFolder::Scenarios);
                    break;
                case FrontAction::NewCustomScenario:
                    open_files(request->action, config::UserFolder::NewScenarios);
                    break;
                case FrontAction::Quit:
                    ask_quit();
                    break;
                default:
                    unhandled_.push_back(*request);
                    break;
            }
        }
        if (TutorialPicker* menu = tutorial_picker())
        {
            if (screen_->current() != FrontMenu::Tutorials || !screen_->visible)
                desktop_.root().remove(*menu);
            else if (auto help = menu->help())
                screen_->show_help(std::move((*help)[0]), std::move((*help)[1]));
        }
        if (CareerBoard* map = career_board())
        {
            if (map->take_quit())
                ask_quit();
            if (map->take_menu())
            {
                desktop_.root().remove(*map);
                screen_->visible = true;
                screen_->open(FrontMenu::Campaigns);
                screen_->settle();
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

        options_.update();
    }

    bool FrontEnd::take_quit()
    {
        return std::exchange(quit_, false);
    }

    bool FrontEnd::take_options_closed()
    {
        return options_.take_closed();
    }

    std::optional<std::string> FrontEnd::take_language()
    {
        return options_.take_language();
    }

    std::optional<std::size_t> FrontEnd::take_screen_size()
    {
        return options_.take_screen_size();
    }

    std::optional<KeyMap> FrontEnd::take_keys()
    {
        return options_.take_keys();
    }

    bool FrontEnd::waiting_for_keys() const
    {
        return options_.waiting_for_keys();
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
