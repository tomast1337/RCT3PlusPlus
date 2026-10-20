// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/options_session.hpp"

#include "core/maybe.hpp"

#include <utility>

namespace rct3::ui
{

    OptionsSession::OptionsSession(Desktop& desktop, OptionsDialogStyle style, const StringTable& strings, config::GameOptions& options, OptionsChoices choices)
        : desktop_(desktop)
        , style_(std::move(style))
        , strings_(strings)
        , options_(options)
        , choices_(std::move(choices))
    {
    }

    std::optional<OptionsDialog&> OptionsSession::window() const
    {
        return desktop_.root().find_as<OptionsDialog>(kName);
    }

    void OptionsSession::open()
    {
        if (auto shown = window())
        {
            desktop_.root().raise(*shown);
            return;
        }
        desktop_.root().add<OptionsDialog>(std::string(kName), style_, strings_, options_, choices_);
        open_ = true;
    }

    void OptionsSession::close()
    {
        if (auto shown = window())
        {
            update(); // What it still holds
            desktop_.root().remove(*shown);
        }
        update();
    }

    void OptionsSession::update()
    {
        auto dialog = window();
        if (!dialog)
        {
            closed_ = closed_ || std::exchange(open_, false);
            return;
        }
        if (auto language = dialog->take_language())
            language_ = std::move(language);
        if (const auto size = dialog->take_screen_size())
        {
            screen_size_ = size;
            choices_.screen_size = size;
        }
        if (auto keys = dialog->take_keys())
        {
            choices_.keys = *keys;
            keys_ = std::move(keys);
        }
    }

    bool OptionsSession::take_closed()
    {
        return std::exchange(closed_, false);
    }

    std::optional<std::string> OptionsSession::take_language()
    {
        return std::exchange(language_, std::nullopt);
    }

    std::optional<std::size_t> OptionsSession::take_screen_size()
    {
        return std::exchange(screen_size_, std::nullopt);
    }

    std::optional<KeyMap> OptionsSession::take_keys()
    {
        return std::exchange(keys_, std::nullopt);
    }

    bool OptionsSession::waiting_for_keys() const
    {
        const auto dialog = window();
        return dialog && dialog->waiting_for_keys();
    }

} // namespace rct3::ui
