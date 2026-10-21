// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/startup_screen.hpp"

#include "logging/log.hpp"
#include "ui/image_set.hpp"
#include "ui/scale.hpp"
#include "ui/screens/loading_screen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string_view>

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

        // The movies a logo may have, best first: only the first release has them.
        struct MovieChoice
        {
            std::string_view logo;
            std::array<std::string_view, 3> files;
        };
        constexpr std::array kMovies {
            MovieChoice { "AtariLogo", { "Movies/Atari.wmv", "Movies/AtariLogo_800x600_30fps.wmv", "Movies/AtariLogo_640x480_15fps.wmv" } },
            MovieChoice { "FrontierLogo", { "Movies/FrontierLogo_800x600_30fps.wmv", "Movies/FrontierLogo_640x480_15fps.wmv", { } } },
        };

        // A movie that cannot be read is left out: its logo's picture shows instead, if the install has one.
        void load_movies(render::Device& device, const config::GameInstall& install, StartupScreen& screen)
        {
            for (const MovieChoice& choice : kMovies)
                for (const std::string_view name : choice.files)
                {
                    if (name.empty())
                        break;
                    const auto file = install.find(name);
                    if (!file)
                        continue;
                    auto movie = video::WmvMovie::open(*file);
                    if (!movie)
                    {
                        logging::logger("startup").warning("startup movie: {}", movie.error());
                        continue;
                    }
                    const std::vector<std::byte> white(static_cast<std::size_t>(movie->width()) * movie->height() * 4, std::byte { 255 });
                    auto texture = device.create_texture({ movie->width(), movie->height(), render::TextureFormat::Rgba8, white, name });
                    if (!texture)
                    {
                        logging::logger("startup").warning("startup movie: {}: {}", name, texture.error());
                        continue;
                    }
                    const ui::UiTexture picture { *texture, static_cast<float>(movie->width()), static_cast<float>(movie->height()) };
                    screen.movies.emplace(std::string(choice.logo), StartupMovie { std::move(*movie), *file, picture, std::nullopt });
                    break;
                }
        }

        std::expected<void, std::string> load(render::Device& device, const config::GameInstall& install, StartupScreen& screen)
        {
            const auto banner = install.find(ui::loading_screen::kLogoFile);
            if (!banner)
                return std::unexpected(std::format("{} not found", ui::loading_screen::kLogoFile));
            auto images = ui::load_image_set(*banner);
            if (!images)
                return std::unexpected(std::format("banner: {}", images.error()));
            auto logos = ui::upload_textures(device, *images);
            if (!logos)
                return std::unexpected(logos.error());
            screen.logos = std::move(*logos);

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

        load_movies(device, install, screen);

        // Steps whose logo is missing are left out rather than shown blank. A movie's step lasts as long as
        // the movie, its last second the step's fade out.
        std::vector<ui::StartupStep> steps;
        for (auto& step : ui::startup_steps(options))
        {
            if (const auto movie = screen.movies.find(step.image); movie != screen.movies.end())
            {
                step.seconds = std::max(static_cast<float>(movie->second.movie.seconds()) - ui::kStartupFadeSeconds, 0.0f);
                steps.push_back(std::move(step));
            }
            else if (screen.logos.contains(step.image))
                steps.push_back(std::move(step));
        }
        screen.sequence = ui::StartupSequence(std::move(steps));
        return screen;
    }

    void update_startup_movie(render::Device& device, StartupScreen& screen, audio::AudioEngine* audio)
    {
        const auto frame = screen.sequence.frame();
        for (auto& [logo, m] : screen.movies)
        {
            const bool current = frame && frame->image == logo;
            if (!current)
            {
                if (m.sound && audio)
                    m.sound->stop(*audio);
                m.sound.reset();
                continue;
            }
            if (m.movie.advance(frame->time))
                device.update_texture(m.picture.handle, std::as_bytes(m.movie.picture()));
            if (!audio)
                continue;
            if (!m.sounded)
            {
                m.sounded = true;
                if (auto sound = audio::MusicPlayer::start(*audio, m.file, 1.0f, false))
                    m.sound = std::move(*sound);
                else
                    logging::logger("startup").warning("startup movie sound: {}", sound.error());
            }
            if (m.sound)
            {
                if (auto fed = m.sound->update(*audio); !fed)
                    logging::logger("startup").warning("startup movie sound: {}", fed.error());
            }
        }
    }

    void stop_startup_sound(StartupScreen& screen, audio::AudioEngine& audio)
    {
        for (auto& [_, m] : screen.movies)
        {
            if (m.sound)
                m.sound->stop(audio);
            m.sound.reset();
        }
    }

    void draw_startup_logo(render::Device& device, StartupScreen& screen)
    {
        const ui::Rect view = begin(device, screen.list, screen.scale);
        screen.list.fill(view, ui::kWhite);
        if (const auto frame = screen.sequence.frame())
        {
            // A movie plays as it is: it fades in and out on its own.
            const auto movie = screen.movies.find(frame->image);
            const auto logo = screen.logos.find(frame->image);
            const ui::UiTexture* shown = movie != screen.movies.end() ? &movie->second.picture : logo != screen.logos.end() ? &logo->second : nullptr;
            if (shown)
            {
                const float fit = std::min(view.width() / shown->width, view.height() / shown->height);
                const float w = shown->width * fit;
                const float h = shown->height * fit;
                const float x = std::round((view.width() - w) / 2);
                const float y = std::round((view.height() - h) / 2);
                const float opacity = movie != screen.movies.end() ? 1.0f : frame->opacity;
                const auto alpha = static_cast<std::uint8_t>(std::lround(opacity * 255));
                screen.list.image({ x, y, x + w, y + h }, ui::texture_id(shown->handle), { 0, 0, 1, 1 }, { 255, 255, 255, alpha });
            }
        }
        ui::render_draw_list(device, screen.renderer, screen.list);
    }

    void draw_loading_screen(render::Device& device, StartupScreen& screen)
    {
        // In pixels, like the park's loading screen. Only the logo: the strings and fonts are what is loading.
        namespace loading = ui::loading_screen;
        const render::Extent extent = device.extent();
        screen.list.clear();
        screen.list.set_scale(1);
        loading::Look look;
        if (const auto logo = screen.logos.find(loading::kLogo); logo != screen.logos.end())
            look.logo = loading::Picture { ui::texture_id(logo->second.handle), logo->second.width, logo->second.height };
        loading::draw(screen.list, { 0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height) }, look, { });
        ui::render_draw_list(device, screen.renderer, screen.list);
    }

    void destroy_startup_screen(render::Device& device, StartupScreen& screen)
    {
        for (auto& [_, m] : screen.movies)
            device.destroy_texture(m.picture.handle);
        ui::destroy_textures(device, screen.logos);
        ui::destroy_ui_renderer(device, screen.renderer);
        screen = { };
    }

} // namespace rct3::scene
