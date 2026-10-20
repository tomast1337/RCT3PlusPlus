// SPDX-License-Identifier: GPL-3.0-or-later
//
// The options window as one desktop keeps it: opened on request (asking again brings it to the front), what
// the user picked in it gathered for the owner, and its closing reported once so the owner saves the
// options. The front end and the park each keep one on their own desktop.
//
// What it offers stays current: once the user picks keys or a screen size, the next window opened shows
// them.
//
// Update it after the desktop each frame. It never keeps the window: windows can close in the middle of a
// desktop update, so it finds its window by name.
#pragma once

#include "config/game_options.hpp"
#include "core/maybe.hpp"
#include "ui/desktop.hpp"
#include "ui/screens/options_dialog.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui
{

    class OptionsSession
    {
    public:
        /// For windows on `desktop`, writing to `options`. The desktop, strings and options must outlive it.
        OptionsSession(Desktop& desktop, OptionsDialogStyle style, const StringTable& strings, config::GameOptions& options, OptionsChoices choices);

        /// Opens the window, or brings it to the front.
        void open();
        /// Closes the window if it is open; its closing is reported as if the user closed it.
        void close();
        /// The window, while it is open.
        [[nodiscard]] std::optional<OptionsDialog&> window() const;

        void update();

        /// What the next window offers.
        [[nodiscard]] OptionsChoices& choices()
        {
            return choices_;
        }
        [[nodiscard]] const OptionsChoices& choices() const
        {
            return choices_;
        }

        /// True once after the window has closed: time to save the options.
        [[nodiscard]] bool take_closed();
        [[nodiscard]] std::optional<std::string> take_language();
        [[nodiscard]] std::optional<std::size_t> take_screen_size();
        [[nodiscard]] std::optional<KeyMap> take_keys();
        /// True while the window waits for keys, which should then do nothing else.
        [[nodiscard]] bool waiting_for_keys() const;

        static constexpr std::string_view kName = "options";

    private:
        Desktop& desktop_;
        OptionsDialogStyle style_;
        const StringTable& strings_;
        config::GameOptions& options_;
        OptionsChoices choices_;
        bool open_ = false;
        bool closed_ = false;
        std::optional<std::string> language_;
        std::optional<std::size_t> screen_size_;
        std::optional<KeyMap> keys_;
    };

} // namespace rct3::ui
