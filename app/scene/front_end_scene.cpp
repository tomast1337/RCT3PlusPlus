// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/front_end_scene.hpp"

#include "config/option_effects.hpp"
#include "logging/log.hpp"
#include "scene/menu_audio.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/scale.hpp"
#include "ui/screens/loading_screen.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        /// The newest logo comes with its own skins, the career map's pictures come in a file per game, and the
        /// loading screen shown while a park loads has a file of its own.
        constexpr std::array<std::string_view, 5> kMoreSkins = {
            "gui/FrontScreen/UpdatedTShirt/FrontScreen_UpdatedTShirt.common.ovl", "gui/FrontScreen/ScenarioLogos/Base/FrontScreen.common.ovl", "gui/FrontScreen/ScenarioLogos/CapeTyphoon/FrontScreen.common.ovl", "gui/FrontScreen/ScenarioLogos/Wild/FrontScreen.common.ovl", ui::loading_screen::kSkinFile,
        };

        /// What the options show for the zoomed-out park's music, as the game does at first.
        constexpr std::string_view kFirstBackgroundMusic = "Summer Air.wma";

        float scale_for(const render::Device& device, const FrontEndScene& scene)
        {
            return ui::interface_scale(static_cast<float>(device.extent().height), scene.scale);
        }

    } // namespace

    std::expected<FrontEndScene, std::string> create_front_end_scene(render::Device& device, const config::GameInstall& install, FrontEndSceneSetup setup)
    {
        const auto log = logging::logger("ui");
        auto assets = ui::load_interface_assets(device, install, setup.language, setup.fallback_font, kMoreSkins);
        if (!assets)
            return std::unexpected(assets.error());

        FrontEndScene scene;
        scene.assets = std::move(*assets);
        scene.slideshow.show.set_pictures(ui::slideshow_pictures(setup.picture_folders, setup.picture_seed));
        scene.picture_folders = std::move(setup.picture_folders);
        scene.picture_seed = setup.picture_seed;
        scene.options_file = std::move(setup.options_file);
        scene.careers.data = setup.data;
        scene.saves.data = std::move(setup.data);
        scene.saves.game_saves = std::move(setup.game_saves);
        if (scene.options_file)
        {
            std::vector<config::FileProblem> problems;
            if (auto loaded = config::GameOptions::load(*scene.options_file, &problems))
                scene.live->options = std::move(*loaded);
            else
                log.warning("options: {}", loaded.error());
            for (const auto& problem : problems)
                log.warning("{}: {}", scene.options_file->string(), problem.message);
        }

        ui::Desktop& desktop = scene.live->desktop;
        desktop.tooltip_style = skins::tooltip_style();
        ui::FrontEndSetup front;
        front.expansions = ui::named_expansions(*scene.assets.strings);
        front.choices.languages = std::move(setup.languages);
        front.choices.language = std::move(setup.language);
        front.choices.screen_sizes = std::move(setup.screen_sizes);
        front.choices.screen_size = setup.screen_size;
        front.choices.keys = std::move(setup.keys);
        front.choices.inert = config::inert_options();
        front.choices.background_music = music_files(install);
        const auto shown = std::ranges::find(front.choices.background_music, kFirstBackgroundMusic);
        if (shown != front.choices.background_music.end())
            front.choices.background_track = static_cast<std::size_t>(shown - front.choices.background_music.begin());
        // The game writes times with am and pm only in English.
        front.twelve_hour = front.choices.language == ui::kDefaultLanguage;
        front.open_options = setup.reload;
        front.settled = setup.reload;
        scene.live->front.emplace(desktop, ui::FrontEndStyle { skins::options_dialog_style(), skins::question_box_style(), skins::file_dialog_style(), skins::career_dialog_style(), skins::career_board_style(), skins::tutorial_picker_style(), skins::front_screen_round() }, *scene.assets.strings, scene.live->options, std::move(front));
        return scene;
    }

    bool update_front_end_scene(render::Device& device, FrontEndScene& scene, const ui::PointerState& pointer_pixels, const ui::KeyboardState& keyboard, float elapsed)
    {
        const float scale = scale_for(device, scene);
        const render::Extent extent = device.extent();
        ui::Desktop& desktop = scene.live->desktop;
        desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        ui::PointerState pointer = pointer_pixels;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, scene.assets, desktop.root().rect().width());
        const bool over = desktop.update(pointer, keyboard, { elapsed, scene.current_fonts });
        update_slideshow_view(device, scene.slideshow, elapsed);
        scene.live->front->update();
        ui::FrontEnd& front = *scene.live->front;
        if (auto picked = update_save_browser(device, scene.saves, front.file_dialog(), front.file_dialogs_opened(), front.file_folder()))
            scene.park_to_play = std::move(picked);
        update_career_browser(device, scene.careers, *scene.live->front, *scene.assets.strings, *scene.assets.install);
        if (scene.careers.slideshow != scene.slideshow_shown)
        {
            // The career map shows the picked scenario's pictures; back on the front screen, the usual ones.
            scene.slideshow_shown = scene.careers.slideshow;
            std::vector<std::filesystem::path> folders;
            if (scene.slideshow_shown)
            {
                if (const auto folder = scene.assets.install->find(*scene.slideshow_shown))
                    folders.push_back(*folder);
            }
            scene.slideshow.show.set_pictures(ui::slideshow_pictures(scene.slideshow_shown ? folders : scene.picture_folders, scene.picture_seed));
        }
        if (ui::TutorialPicker* picker = scene.live->front->tutorial_picker())
        {
            // A new menu: say which tutorials' parks are there.
            if (scene.live->front->tutorial_pickers_opened() != scene.tutorials_seen)
            {
                scene.tutorials_seen = scene.live->front->tutorial_pickers_opened();
                for (std::size_t i = 0; i < picker->size(); ++i)
                    picker->set_available(i, scene.assets.install->find(picker->tutorial(i).park).has_value());
            }
            if (const auto start = picker->take_start())
                scene.park_to_play = scene.assets.install->find(picker->tutorial(*start).park);
        }
        if (scene.live->front->take_options_closed())
            save_front_end_options(scene);
        while (const auto request = scene.live->front->take_unhandled())
            logging::logger("ui").info("front screen: choice {} (pack {}) does nothing yet", static_cast<int>(request->action), request->pack);
        return over;
    }

    FrontEndRequests take_front_end_requests(FrontEndScene& scene)
    {
        ui::FrontEnd& front = *scene.live->front;
        FrontEndRequests r;
        r.quit = front.take_quit();
        r.screen_size = front.take_screen_size();
        r.language = front.take_language();
        r.keys = front.take_keys();
        r.park = std::exchange(scene.park_to_play, std::nullopt);
        const bool fullscreen = scene.live->options.flag(config::GameOption::Fullscreen);
        if (fullscreen != scene.fullscreen)
        {
            scene.fullscreen = fullscreen;
            r.fullscreen = fullscreen;
        }
        return r;
    }

    bool front_end_waiting_for_keys(const FrontEndScene& scene)
    {
        return scene.live->front->waiting_for_keys();
    }

    void save_front_end_options(const FrontEndScene& scene)
    {
        if (!scene.options_file)
            return;
        if (auto saved = scene.live->options.save(*scene.options_file); !saved)
            logging::logger("ui").warning("options: {}", saved.error());
    }

    void draw_front_end_scene(render::Device& device, FrontEndScene& scene)
    {
        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { scene.list, scene.assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts };
        // The pictures show through the front screen's open corner and circle.
        const ui::Rect& screen = scene.live->desktop.root().rect();
        draw_slideshow_view(scene.slideshow, scene.list, ui::slideshow_rect(screen.width(), screen.height()));
        scene.live->desktop.draw(context);
        ui::render_draw_list(device, scene.assets.renderer, scene.list);
    }

    void destroy_front_end_scene(render::Device& device, FrontEndScene& scene)
    {
        if (scene.live && scene.live->front && scene.live->front->options_window() != nullptr)
            save_front_end_options(scene);
        destroy_slideshow_view(device, scene.slideshow);
        destroy_save_browser(device, scene.saves);
        destroy_career_browser(device, scene.careers);
        ui::destroy_interface_assets(device, scene.assets);
        scene = { };
    }

} // namespace rct3::scene
