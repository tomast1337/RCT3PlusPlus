// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/front_end_scene.hpp"

#include "logging/log.hpp"
#include "ui/game_style.hpp"
#include "ui/scale.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        /// The newest logo comes with its own skins.
        constexpr std::array<std::string_view, 1> kMoreSkins = { "gui/FrontScreen/UpdatedTShirt/FrontScreen_UpdatedTShirt.common.ovl" };

        float scale_for(const render::Device& device, const FrontEndScene& scene)
        {
            return ui::interface_scale(static_cast<float>(device.extent().height), scene.scale);
        }

        void save_options(const FrontEndScene& scene)
        {
            if (!scene.options_file)
                return;
            if (auto saved = scene.live->options.save(*scene.options_file); !saved)
                logging::logger("ui").warning("options: {}", saved.error());
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
        scene.options_file = std::move(setup.options_file);
        if (scene.options_file)
        {
            std::vector<config::GameOptionsProblem> problems;
            if (auto loaded = config::GameOptions::load(*scene.options_file, &problems))
                scene.live->options = std::move(*loaded);
            else
                log.warning("options: {}", loaded.error());
            for (const auto& problem : problems)
                log.warning("{}:{}: {}", scene.options_file->string(), problem.line, problem.message);
        }

        ui::Desktop& desktop = scene.live->desktop;
        desktop.tooltip_style = skins::tooltip_style();
        ui::FrontEndSetup front;
        front.expansions = ui::named_expansions(*scene.assets.strings);
        front.choices.languages = std::move(setup.languages);
        front.choices.language = std::move(setup.language);
        front.choices.screen_sizes = std::move(setup.screen_sizes);
        front.choices.screen_size = setup.screen_size;
        front.open_options = setup.reload;
        front.settled = setup.reload;
        scene.live->front.emplace(desktop, ui::FrontEndStyle { skins::options_dialog_style(), skins::question_box_style() }, *scene.assets.strings, scene.live->options, std::move(front));
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
        scene.live->front->update();
        if (scene.live->front->take_options_closed())
            save_options(scene);
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
        const bool fullscreen = scene.live->options.flag(config::GameOption::Fullscreen);
        if (fullscreen != scene.fullscreen)
        {
            scene.fullscreen = fullscreen;
            r.fullscreen = fullscreen;
        }
        return r;
    }

    void draw_front_end_scene(render::Device& device, FrontEndScene& scene)
    {
        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { scene.list, scene.assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts };
        scene.live->desktop.draw(context);
        ui::render_draw_list(device, scene.assets.renderer, scene.list);
    }

    void destroy_front_end_scene(render::Device& device, FrontEndScene& scene)
    {
        if (scene.live && scene.live->front && scene.live->front->options_window() != nullptr)
            save_options(scene);
        ui::destroy_interface_assets(device, scene.assets);
        scene = { };
    }

} // namespace rct3::scene
