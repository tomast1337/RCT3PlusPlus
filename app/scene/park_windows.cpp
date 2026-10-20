// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_windows.hpp"

#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/screens/about_window.hpp"
#include "ui/screens/file_dialog.hpp"

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
        constexpr std::string_view kFilesName = "park_files";
        /// The folder the park's games are saved to and loaded from: the sandbox parks, as in the game.
        constexpr config::UserFolder kFilesFolder = config::UserFolder::Parks;
        /// A new sandbox starts from the blank landscape in the game's folder; its first save is offered this name.
        constexpr std::string_view kBlankLandscape = "BlankLandscape";
        constexpr std::u16string_view kNewSandbox = u"New Sandbox";
        /// An add-on pack is installed when its files are; numbered as the game numbers them.
        constexpr std::array<std::string_view, 2> kPackFiles = { "ContentPacks/ContentPack0000.common.ovl", "ContentPacks/ContentPack0001.common.ovl" };

        void open_about(ParkScene& park, const FrontEndScene& front)
        {
            ui::Widget& root = park.desktop->root();
            if (auto shown = root.find(kAboutName))
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
            root.add<ui::GameInfoBox>(std::string(kAboutName), skins::about_window_style(), *front.assets.strings, packs, park.seed);
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

        std::optional<ui::FileDialog&> file_dialog(const ParkScene& park)
        {
            return park.desktop->root().find_as<ui::FileDialog>(kFilesName);
        }

        void open_files(ParkScene& park, const FrontEndScene& front, ui::FileDialogMode mode)
        {
            ui::Widget& root = park.desktop->root();
            if (auto shown = file_dialog(park))
            {
                if (shown->mode() == mode && !shown->closing())
                {
                    root.raise(*shown);
                    return;
                }
                root.remove(*shown);
            }
            park.saves.data = front.saves.data;
            park.saves.game_saves = front.saves.game_saves;
            ++park.file_windows_opened;
            auto& dialog = root.add<ui::FileDialog>(std::string(kFilesName), skins::file_dialog_style(), *front.assets.strings, mode, front.assets.fonts.language == ui::kDefaultLanguage);
            dialog.set_file_name(park.file.stem() == kBlankLandscape ? std::u16string(kNewSandbox) : park.file.filename().u16string());
        }

    } // namespace

    void update_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front)
    {
        for (const ui::ToolItem item : std::exchange(park.window_requests, { }))
        {
            if (item == ui::ToolItem::About)
                open_about(park, front);
            else if (item == ui::ToolItem::Options)
                open_options(park, front);
            else if (item == ui::ToolItem::Save)
                open_files(park, front, ui::FileDialogMode::Save);
            else if (item == ui::ToolItem::Load)
                open_files(park, front, ui::FileDialogMode::Load);
        }
        if (auto picked = update_save_browser(device, park.saves, file_dialog(park), park.file_windows_opened, kFilesFolder, [&park](const std::filesystem::path& path) {
                // Saved again, it is offered under this name.
                park.file = path;
                return save_park(park);
            }))
            park.park_to_load = std::move(picked);
        if (const auto follow = std::exchange(park.footpaths.follow_changed, std::nullopt))
        {
            front.live->options.set(config::GameOption::PathCameraFollow, *follow ? 1.0f : 0.0f);
            save_front_end_options(front);
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
        r.park = std::exchange(park.park_to_load, std::nullopt);
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

    void close_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front)
    {
        destroy_save_browser(device, park.saves);
        if (!park.options)
            return;
        park.options->close();
        if (park.options->take_closed())
            save_front_end_options(front);
        park.options.reset();
    }

} // namespace rct3::scene
