// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end: the front screen and the windows it opens, on one desktop. Options opens the options
// window (once; asking again brings it to the front), and Quit asks first. Loading a sandbox park or a
// custom scenario, or starting a new custom scenario, opens the window that loads games on the folder the
// game keeps those in; the owner fills it with the folder's files and acts on what the user picks there.
// A game's career button opens the window that picks a career of that game, which the owner fills too. What needs more than the
// interface goes to the owner: quitting, a new language or screen size, the options to save once their
// window has closed, and the menu choices nothing handles yet.
//
// Update it after the desktop each frame. It never keeps a window it did not see this frame: windows can
// close in the middle of a desktop update, so it finds them by name.
#pragma once

#include "config/game_options.hpp"
#include "config/user_data.hpp"
#include "ui/desktop.hpp"
#include "ui/screens/career_dialog.hpp"
#include "ui/screens/file_dialog.hpp"
#include "ui/screens/front_screen.hpp"
#include "ui/screens/options_dialog.hpp"
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
        [[nodiscard]] OptionsDialog* options_window() const;
        /// The window loading a game, while it is open.
        [[nodiscard]] FileDialog* file_dialog() const;
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
        [[nodiscard]] CareerDialog* career_dialog() const;
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
        /// Counts the windows loading a game opened, so the owner tells a new one from the last.
        [[nodiscard]] std::size_t file_dialogs_opened() const
        {
            return file_dialogs_opened_;
        }
        [[nodiscard]] QuestionBox* question() const
        {
            return question_;
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

        static constexpr std::string_view kOptionsName = "options";
        static constexpr std::string_view kQuestionName = "quit";
        static constexpr std::string_view kFilesName = "load_game";
        static constexpr std::string_view kCareersName = "careers";

    private:
        void open_options();
        void open_files(FrontAction action, config::UserFolder folder);
        void open_careers(int pack);

        Desktop& desktop_;
        FrontEndStyle style_;
        const StringTable& strings_;
        config::GameOptions& options_;
        FrontEndSetup setup_;
        FrontScreen* screen_ = nullptr;
        QuestionBox* question_ = nullptr; // Only closes when take_answer() closes it
        config::UserFolder file_folder_ = config::UserFolder::Parks;
        FrontAction file_action_ = FrontAction::LoadSandbox;
        std::size_t file_dialogs_opened_ = 0;
        int career_pack_ = 0;
        std::size_t career_dialogs_opened_ = 0;
        bool options_open_ = false;
        bool options_closed_ = false;
        bool quit_ = false;
        std::optional<std::string> language_;
        std::optional<std::size_t> screen_size_;
        std::optional<KeyMap> keys_;
        std::vector<FrontRequest> unhandled_;
    };

} // namespace rct3::ui
