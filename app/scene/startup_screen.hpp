// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game shows before the front end: the logo sequence, then the loading screen while data loads.
// Logos fill as much of the window as they can without changing shape, centred on white (their art is
// dark on transparent). The loading screen is the one shown while a park loads (ui/screens/loading_screen.hpp),
// without text or bar: the strings and fonts are among what is loading.
//
// The first release has its publisher's and developer's logos as movies (Movies/, Windows Media Video 9) with
// their sound; where the install has one, it plays in place of that logo's picture, as long as the movie
// runs, and its sound with it.
#pragma once

#include "audio/audio.hpp"
#include "audio/music_player.hpp"
#include "config/game_install.hpp"
#include "render/device.hpp"
#include "ui/draw_list.hpp"
#include "ui/skin_set.hpp"
#include "ui/startup_sequence.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"
#include "video/wmv_movie.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace rct3::scene
{

    struct StartupMovie
    {
        video::WmvMovie movie;
        std::filesystem::path file; ///< Also holds the sound
        ui::UiTexture picture;
        std::optional<audio::MusicPlayer> sound;
        bool sounded = false; ///< Started once; not again
    };

    struct StartupScreen
    {
        std::map<std::string, StartupMovie, std::less<>> movies; ///< By the logo they replace
        ui::UiRenderer renderer;
        ui::UiTextures logos;
        ui::UiTextures loading_textures;
        std::optional<ui::NamedSkin> loading;
        ui::StartupSequence sequence { { } };
        ui::DrawList list;
        std::optional<float> scale; ///< Chosen interface scale; nothing picks one from the window height
    };

    /// Loads the banner logos and the loading screen from the install.
    [[nodiscard]] std::expected<StartupScreen, std::string> create_startup_screen(render::Device& device, const config::GameInstall& install, const ui::StartupOptions& options);

    /// Brings the current logo's movie, if it is one, to its time: its picture into its texture and its
    /// sound into `audio` (none: silent); a movie's sound stops once its step is over. Call between
    /// begin_frame and the pass draw_startup_logo records into.
    void update_startup_movie(render::Device& device, StartupScreen& screen, audio::AudioEngine* audio);

    /// Stops any movie's sound, as when the logos are skipped.
    void stop_startup_sound(StartupScreen& screen, audio::AudioEngine& audio);

    /// Records the current logo into the current pass. Call while `screen.sequence` is not finished.
    void draw_startup_logo(render::Device& device, StartupScreen& screen);

    /// Records the loading screen into the current pass.
    void draw_loading_screen(render::Device& device, StartupScreen& screen);

    void destroy_startup_screen(render::Device& device, StartupScreen& screen);

} // namespace rct3::scene
