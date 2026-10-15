// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/startup_screen.hpp"

#include "ui/image_set.hpp"
#include "ui/scale.hpp"
#include "ui/screens/loading_screen.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::scene
{

    namespace
    {

        // The window in logical units, after choosing the scale.
        ui::Rect begin(render::Device& device, ui::DrawList& list, std::optional<float> chosen)
        {
            const render::Extent extent = device.extent();
            list.clear();
            list.set_scale(ui::interface_scale(static_cast<float>(extent.height), chosen));
            return { 0, 0, static_cast<float>(extent.width) / list.scale(), static_cast<float>(extent.height) / list.scale() };
        }

        std::expected<void, std::string> load(render::Device& device, const config::GameInstall& install, StartupScreen& screen)
        {
            const auto banner = install.find("gui/Banner/Banner.common.ovl");
            if (!banner)
                return std::unexpected("gui/Banner/Banner.common.ovl not found");
            auto images = ui::load_image_set(*banner);
            if (!images)
                return std::unexpected("banner: " + images.error());
            auto logos = ui::upload_textures(device, *images);
            if (!logos)
                return std::unexpected(logos.error());
            screen.logos = std::move(*logos);

            // The loading screen is optional: without it the screen stays black.
            if (const auto path = install.find(ui::loading_screen::kSkinFile))
            {
                auto set = ui::load_skin_set(*path);
                if (!set)
                    return std::unexpected("loading screen: " + set.error());
                auto textures = ui::upload_textures(device, set->images);
                if (!textures)
                    return std::unexpected(textures.error());
                screen.loading_textures = std::move(*textures);
                if (const auto skin = set->skins.find(std::string(ui::loading_screen::kBand)); skin != set->skins.end())
                    screen.loading = skin->second;
            }
            return { };
        }

    } // namespace

    std::expected<StartupScreen, std::string> create_startup_screen(render::Device& device, const config::GameInstall& install, const ui::StartupOptions& options)
    {
        StartupScreen screen;
        auto renderer = ui::create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        screen.renderer = *renderer;
        if (auto loaded = load(device, install, screen); !loaded)
        {
            destroy_startup_screen(device, screen);
            return std::unexpected(loaded.error());
        }

        // Steps whose logo is missing are left out rather than shown blank.
        std::vector<ui::StartupStep> steps;
        for (auto& step : ui::startup_steps(options))
        {
            if (screen.logos.contains(step.image))
                steps.push_back(std::move(step));
        }
        screen.sequence = ui::StartupSequence(std::move(steps));
        return screen;
    }

    void draw_startup_logo(render::Device& device, StartupScreen& screen)
    {
        const ui::Rect view = begin(device, screen.list, screen.scale);
        screen.list.fill(view, ui::kWhite);
        if (const auto frame = screen.sequence.frame())
        {
            const auto logo = screen.logos.find(frame->image);
            if (logo != screen.logos.end())
            {
                const float fit = std::min(view.width() / logo->second.width, view.height() / logo->second.height);
                const float w = logo->second.width * fit;
                const float h = logo->second.height * fit;
                const float x = std::round((view.width() - w) / 2);
                const float y = std::round((view.height() - h) / 2);
                const auto alpha = static_cast<std::uint8_t>(std::lround(frame->opacity * 255));
                screen.list.image({ x, y, x + w, y + h }, ui::texture_id(logo->second.handle), { 0, 0, 1, 1 }, { 255, 255, 255, alpha });
            }
        }
        ui::render_draw_list(device, screen.renderer, screen.list);
    }

    void draw_loading_screen(render::Device& device, StartupScreen& screen)
    {
        // In pixels, like the park's loading screen: the band is fitted to the window.
        const render::Extent extent = device.extent();
        screen.list.clear();
        screen.list.set_scale(1);
        std::optional<ui::SkinRef> band;
        if (screen.loading)
        {
            if (const auto texture = screen.loading_textures.find(screen.loading->texture); texture != screen.loading_textures.end())
                band = ui::SkinRef { screen.loading->layout, ui::texture_id(texture->second.handle), texture->second.width, texture->second.height };
        }
        // Nothing to name and no steps to count yet: the strings and fonts are what is being loaded.
        const ui::loading_screen::Look look { .band = band ? &*band : nullptr };
        ui::loading_screen::draw(screen.list, { 0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height) }, look, { });
        ui::render_draw_list(device, screen.renderer, screen.list);
    }

    void destroy_startup_screen(render::Device& device, StartupScreen& screen)
    {
        ui::destroy_textures(device, screen.logos);
        ui::destroy_textures(device, screen.loading_textures);
        ui::destroy_ui_renderer(device, screen.renderer);
        screen = { };
    }

} // namespace rct3::scene
