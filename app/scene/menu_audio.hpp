// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the menus sound like: the run's menu track (menu_music.hpp) played round and round from the game's
// music folder, and the buttons' clicks, a press sound and a release sound each picked at random from three
// in the game's main overlay, along with the alert for a new message in the park. The music follows the
// music volume, the clicks and the alert the interface volume; changes are heard at once. Whatever of it the
// game does not have is left out.
#pragma once

#include "audio/audio.hpp"
#include "audio/music_player.hpp"
#include "config/game_install.hpp"
#include "config/game_options.hpp"
#include "ui/interface_sound.hpp"

#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace rct3::scene
{

    struct MenuAudio
    {
        std::optional<audio::MusicPlayer> music;
        float music_volume = -1; // As last set; below 0 before the first update
        std::vector<audio::SoundBuffer> presses;
        std::vector<audio::SoundBuffer> releases;
        std::vector<audio::SoundBuffer> messages; ///< The park's new message alert
        std::minstd_rand random;
    };

    /// The names of the files in the game's music folder that it can play, sorted; nothing without the folder.
    [[nodiscard]] std::vector<std::string> music_files(const config::GameInstall& install);

    /// Picks the run's track, which moves the menu music's cycle on in `options`, and starts it.
    [[nodiscard]] MenuAudio start_menu_audio(audio::AudioEngine& engine, const config::GameInstall& install, config::GameOptions& options, std::uint32_t seed);

    /// Keeps the music going at the volume of `options` and plays the interface's sounds.
    void update_menu_audio(audio::AudioEngine& engine, MenuAudio& menu, const config::GameOptions& options, std::span<const ui::InterfaceSound> sounds);

    void stop_menu_audio(audio::AudioEngine& engine, MenuAudio& menu);

} // namespace rct3::scene
