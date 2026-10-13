// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end: the front screen and the windows it opens, on one desktop. Options opens the options
// window (once; asking again brings it to the front), and Quit asks first. What needs more than the
// interface goes to the owner: quitting, a new language or screen size, the options to save once their
// window has closed, and the menu choices nothing handles yet.
//
// Update it after the desktop each frame. It never keeps a window it did not see this frame: windows can
// close in the middle of a desktop update, so it finds them by name.
#pragma once

#include "config/game_options.hpp"
#include "ui/desktop.hpp"
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
    };

    struct FrontEndSetup
    {
        std::array<bool, kExpansionCount> expansions { };
        OptionsChoices choices;    ///< For the options window; its language is the one in use
        bool open_options = false; ///< Start with the options window open, e.g. after a language change
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
        /// Menu choices the front end cannot carry out, oldest first.
        [[nodiscard]] std::optional<FrontRequest> take_unhandled();

        static constexpr std::string_view kOptionsName = "options";
        static constexpr std::string_view kQuestionName = "quit";

    private:
        void open_options();

        Desktop& desktop_;
        FrontEndStyle style_;
        const StringTable& strings_;
        config::GameOptions& options_;
        FrontEndSetup setup_;
        FrontScreen* screen_ = nullptr;
        QuestionBox* question_ = nullptr; // Only closes when take_answer() closes it
        bool options_open_ = false;
        bool options_closed_ = false;
        bool quit_ = false;
        std::optional<std::string> language_;
        std::optional<std::size_t> screen_size_;
        std::vector<FrontRequest> unhandled_;
    };

} // namespace rct3::ui
