// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_windows.hpp"

#include "ui/game_style.hpp"
#include "ui/screens/about_window.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kAboutName = "about";
        /// An add-on pack is installed when its files are; numbered as the game numbers them.
        constexpr std::array<std::string_view, 2> kPackFiles = { "ContentPacks/ContentPack0000.common.ovl", "ContentPacks/ContentPack0001.common.ovl" };

        void open_about(ParkScene& park, const FrontEndScene& front)
        {
            ui::Widget& root = park.desktop->root();
            if (ui::Widget* shown = root.find(kAboutName))
            {
                root.raise(*shown);
                return;
            }
            ui::AddOnPacks packs;
            for (std::size_t i = 0; i < kPackFiles.size(); ++i)
            {
                if (front.assets.install && front.assets.install->find(kPackFiles.at(i)))
                    packs.push_back(static_cast<int>(i));
            }
            root.add<ui::AboutWindow>(std::string(kAboutName), skins::about_window_style(), *front.assets.strings, packs, park.seed);
        }

        void open_options(ParkScene& park, FrontEndScene& front)
        {
            if (!park.options)
            {
                // What the front end's window offers, its keys and screen size as they are now.
                ui::OptionsChoices choices = front.live->front->options().choices();
                choices.language_locked = true;
                park.options = std::make_unique<ui::OptionsSession>(*park.desktop, skins::options_dialog_style(), *front.assets.strings, front.live->options, std::move(choices));
            }
            park.options->open();
        }

    } // namespace

    void update_park_windows(ParkScene& park, FrontEndScene& front)
    {
        for (const ui::ToolItem item : std::exchange(park.window_requests, { }))
        {
            if (item == ui::ToolItem::About)
                open_about(park, front);
            else if (item == ui::ToolItem::Options)
                open_options(park, front);
        }
        if (!park.options)
            return;
        park.options->update();
        if (park.options->take_closed())
            save_front_end_options(front);
    }

    FrontEndRequests take_park_window_requests(ParkScene& park, FrontEndScene& front)
    {
        FrontEndRequests r;
        if (park.options)
        {
            ui::OptionsChoices& shared = front.live->front->options().choices();
            r.screen_size = park.options->take_screen_size();
            if (r.screen_size)
                shared.screen_size = r.screen_size;
            r.keys = park.options->take_keys();
            if (r.keys)
                shared.keys = *r.keys;
        }
        const bool fullscreen = front.live->options.flag(config::GameOption::Fullscreen);
        if (fullscreen != front.fullscreen)
        {
            front.fullscreen = fullscreen;
            r.fullscreen = fullscreen;
        }
        return r;
    }

    bool park_waiting_for_keys(const ParkScene& park)
    {
        return park.options != nullptr && park.options->waiting_for_keys();
    }

    void close_park_windows(ParkScene& park, FrontEndScene& front)
    {
        if (!park.options)
            return;
        park.options->close();
        if (park.options->take_closed())
            save_front_end_options(front);
        park.options.reset();
    }

} // namespace rct3::scene
