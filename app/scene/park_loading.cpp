// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_loading.hpp"

#include "ui/screens/loading_screen.hpp"
#include "ui/ui_renderer.hpp"

namespace rct3::scene
{

    std::string_view park_load_text(ParkLoadStage stage)
    {
        switch (stage)
        {
            case ParkLoadStage::ReadFile:
                return "LoadingScreen_LoadResources";
            case ParkLoadStage::Ground:
                return "LoadingScreen_Terrain";
            case ParkLoadStage::Objects:
                return "LoadingScreen_LoadObjects";
            case ParkLoadStage::Scene:
                return "CacheWarmingMessage";
        }
        return { };
    }

    void draw_park_loading(render::Device& device, ui::InterfaceAssets& assets, ui::DrawList& list, ParkLoadStage stage)
    {
        namespace loading = ui::loading_screen;
        const render::Extent extent = device.extent();
        const ui::Rect screen { 0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height) };
        list.clear();
        // In pixels: the band is fitted to the window, and text keeps its font's size, as in the game's menus.
        list.set_scale(1);
        const ui::FontSet& fonts = ui::fonts_for(device, assets, screen.width());
        const loading::Look look {
            .band = assets.skins.find(loading::band_for(assets.fonts.language)),
            .bar_left = assets.skins.find(loading::kBarLeft),
            .bar_right = assets.skins.find(loading::kBarRight),
            .heading_font = fonts.get(ui::FontRole::Large),
            .step_font = fonts.get(ui::FontRole::Medium),
        };
        const float progress = loading::stage_progress(static_cast<std::size_t>(stage), kParkLoadStages);
        const std::u16string step = loading::step_line(assets.strings->get(park_load_text(stage)), progress);
        loading::draw(list, screen, look, { assets.strings->get(loading::kHeadingText), step, progress });
        ui::render_draw_list(device, assets.renderer, list);
    }

} // namespace rct3::scene
