// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end: the front screen and the windows it opens, on one desktop. Options opens the options
// window (once; asking again brings it to the front), and Quit asks first. Loading a sandbox park or a
// custom scenario, or starting a new custom scenario, opens the window that loads games on the folder the
// game keeps those in; the owner fills it with the folder's files and acts on what the user picks there.
// A game's career button opens the window that picks a career of that game, which the owner fills too;
// the owner opens the career map in place of the front screen once a career is loaded, and the map's
// return to the main menu brings the front screen back on the menu of games. A game's tutorial button
// shows that game's tutorials over the screen until another game's is pressed or the menu of games is
// left; the owner says which tutorials' parks are there and starts the one taken. What needs more than the
// interface goes to the owner: quitting, a new language or screen size, the options to save once their
// window has closed, and the menu choices nothing handles yet.
//
// Update it after the desktop each frame. It never keeps a window it did not see this frame: windows can
// close in the middle of a desktop update, so it finds them by name.
#pragma once

#include "config/game_options.hpp"
#include "config/user_data.hpp"
#include "core/maybe.hpp"
#include "ui/desktop.hpp"
#include "ui/screens/career_board.hpp"
#include "ui/screens/career_dialog.hpp"
#include "ui/screens/file_dialog.hpp"
#include "ui/screens/front_screen.hpp"
#include "ui/screens/options_dialog.hpp"
#include "ui/screens/options_session.hpp"
#include "ui/screens/tutorial_picker.hpp"
#include "ui/widgets/question_box.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct FrontEndStyle
    {
        OptionsDialogStyle options;
        QuestionBoxStyle question;
        FileDialogStyle files;
        CareerDialogStyle careers;
        CareerBoardStyle map;
        TutorialPickerStyle tutorials;
        bool round = false; ///< The front screen laid out at one scale, for a backdrop drawn in code; sets the map's and tutorials' too
    };

    struct FrontEndSetup
    {
        std::array<bool, kExpansionCount> expansions { };
        OptionsChoices choices;    ///< For the options window; its language is the one in use
        bool open_options = false; ///< Start with the options window open, e.g. after a language change
        bool settled = false;      ///< Start without the front screen's opening animation
        bool twelve_hour = true;   ///< Times with am and pm, as the game shows them in English
    };

    class FrontEnd
    {
    public:
        /// Adds the front screen to `desktop`. The strings and options must outlive the front end.
        FrontEnd(Desktop& desktop, FrontEndStyle style, const StringTable& strings, config::GameOptions& options, FrontEndSetup setup);

        void update();

        [[nodiscard]] FrontScreen& screen() const
        {
            return *screen_;
        }
        [[nodiscard]] std::optional<OptionsDialog&> options_window() const;
        /// The options window and what it offers, e.g. for a park to open its own with the same choices.
        [[nodiscard]] OptionsSession& options()
        {
            return options_;
        }
        /// The window loading a game, while it is open.
        [[nodiscard]] std::optional<FileDialog&> file_dialog() const;
        /// The folder the window loading a game shows.
        [[nodiscard]] config::UserFolder file_folder() const
        {
            return file_folder_;
        }
        /// The menu choice that opened it.
        [[nodiscard]] FrontAction file_action() const
        {
            return file_action_;
        }
        /// The window picking a career, while it is open.
        [[nodiscard]] std::optional<CareerDialog&> career_dialog() const;
        /// The game whose careers it shows: 0 the original, then each expansion.
        [[nodiscard]] int career_pack() const
        {
            return career_pack_;
        }
        /// Counts the windows picking a career opened, so the owner tells a new one from the last.
        [[nodiscard]] std::size_t career_dialogs_opened() const
        {
            return career_dialogs_opened_;
        }
        /// Shows the career map of `scenarios` in place of the front screen, `picked` first, and closes the
        /// window picking a career.
        CareerBoard& open_career_board(std::vector<CareerBoardScenario> scenarios, std::size_t picked);
        /// The career map, while it is shown.
        [[nodiscard]] std::optional<CareerBoard&> career_board() const;
        /// Counts the career maps opened, so the owner tells a new one from the last.
        [[nodiscard]] std::size_t career_boards_opened() const
        {
            return career_boards_opened_;
        }
        /// The tutorials shown over the screen, while they are.
        [[nodiscard]] std::optional<TutorialPicker&> tutorial_picker() const;
        /// The game whose tutorials they are: 0 the original, then each expansion.
        [[nodiscard]] int tutorial_pack() const
        {
            return tutorial_pack_;
        }
        /// Counts the tutorial menus opened, so the owner tells a new one from the last.
        [[nodiscard]] std::size_t tutorial_pickers_opened() const
        {
            return tutorial_pickers_opened_;
        }
        /// Counts the windows loading a game opened, so the owner tells a new one from the last.
        [[nodiscard]] std::size_t file_dialogs_opened() const
        {
            return file_dialogs_opened_;
        }
        [[nodiscard]] std::optional<QuestionBox&> question() const
        {
            return maybe(question_);
        }

        /// True once the user has confirmed quitting.
        [[nodiscard]] bool take_quit();
        /// True once after the options window has closed: time to save the options.
        [[nodiscard]] bool take_options_closed();
        [[nodiscard]] std::optional<std::string> take_language();
        [[nodiscard]] std::optional<std::size_t> take_screen_size();
        [[nodiscard]] std::optional<KeyMap> take_keys();
        /// True while the options window waits for keys, which should then do nothing else.
        [[nodiscard]] bool waiting_for_keys() const;
        /// Menu choices the front end cannot carry out, oldest first.
        [[nodiscard]] std::optional<FrontRequest> take_unhandled();

        static constexpr std::string_view kOptionsName = OptionsSession::kName;
        static constexpr std::string_view kQuestionName = "quit";
        static constexpr std::string_view kFilesName = "load_game";
        static constexpr std::string_view kCareersName = "careers";
        static constexpr std::string_view kCareerBoardName = "career_board";
        static constexpr std::string_view kTutorialsName = "tutorials";

    private:
        void open_options();
        void open_files(FrontAction action, config::UserFolder folder);
        void open_careers(int pack);
        void open_tutorials(int pack);
        void ask_quit();

        Desktop& desktop_;
        FrontEndStyle style_;
        const StringTable& strings_;
        FrontEndSetup setup_;
        OptionsSession options_;
        FrontScreen* screen_ = nullptr;
        QuestionBox* question_ = nullptr; // Only closes when take_answer() closes it
        config::UserFolder file_folder_ = config::UserFolder::Parks;
        FrontAction file_action_ = FrontAction::LoadSandbox;
        std::size_t file_dialogs_opened_ = 0;
        int career_pack_ = 0;
        std::size_t career_dialogs_opened_ = 0;
        std::size_t career_boards_opened_ = 0;
        int tutorial_pack_ = 0;
        std::size_t tutorial_pickers_opened_ = 0;
        bool quit_ = false;
        std::vector<FrontRequest> unhandled_;
    };

} // namespace rct3::ui
