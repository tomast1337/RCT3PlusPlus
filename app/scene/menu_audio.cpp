// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/menu_audio.hpp"

#include "audio/menu_music.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/sound.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace rct3::scene
{

    namespace
    {

        constexpr std::string_view kMusicFolder = "Music";
        constexpr std::string_view kMusicExtension = ".wma";
        constexpr std::string_view kClickOverlay = "Main.common.ovl";
        constexpr std::array<std::string_view, 3> kPressSounds = { "BClick1", "BClick2", "BClick3" };
        constexpr std::array<std::string_view, 3> kReleaseSounds = { "BRelese1", "BRelese2", "BRelese3" };
        constexpr std::array<std::string_view, 1> kMessageSounds = { "MessageAlert02" };

        void pick_cycle(MenuAudio& menu, const config::GameInstall& install, config::GameOptions& options)
        {
            const auto log = logging::logger("audio");
            const auto folder = install.find(kMusicFolder);
            if (!folder)
            {
                log.warning("no menu music: the game's music folder is missing");
                return;
            }
            menu.folder = *folder;
            menu.files = music_files(install);
            const auto& files = menu.files;
            const std::optional<int> place = options.flag(config::GameOption::CycleMenuMusic) ? std::optional(options.choice(config::GameOption::MenuTrack)) : std::nullopt;
            const auto pick = audio::pick_menu_music(place, audio::is_halloween(std::chrono::system_clock::now()), files);
            if (pick.next_place)
                options.set(config::GameOption::MenuTrack, static_cast<float>(*pick.next_place));
            if (!pick.file)
                log.warning("no menu music: {} is missing", pick.track);
            menu.cycle_file = pick.file;
        }

        /// Plays what `options` ask for, unless it already plays.
        void follow_choice(audio::AudioEngine& engine, MenuAudio& menu, const config::GameOptions& options, bool in_park)
        {
            if (menu.folder.empty())
                return;
            const audio::MenuMusicWish menu_wish = audio::menu_music_wish(options.flag(config::GameOption::ShuffleBackgroundMusic), options.background_music(), menu.files);
            const audio::MenuMusicWish wish = in_park ? audio::park_music_wish(menu_wish) : menu_wish;
            if (menu.wish == wish)
                return;
            menu.wish = wish;
            if (menu.music)
                menu.music->stop(engine);
            menu.music.reset();
            menu.music_volume = -1;
            std::optional<std::size_t> file;
            switch (wish.source)
            {
                case audio::MenuMusicSource::Cycle:
                    file = menu.cycle_file;
                    break;
                case audio::MenuMusicSource::Track:
                    file = wish.file;
                    break;
                case audio::MenuMusicSource::Shuffle:
                    if (!menu.files.empty())
                        file = std::uniform_int_distribution<std::size_t>(0, menu.files.size() - 1)(menu.random);
                    break;
                case audio::MenuMusicSource::Silent:
                    break;
            }
            if (!file)
                return;
            const auto log = logging::logger("audio");
            auto music = audio::MusicPlayer::start(engine, menu.folder / menu.files[*file], 0);
            if (!music)
            {
                log.warning("no menu music: {}", music.error());
                return;
            }
            log.info("menu music: {}", menu.files[*file]);
            menu.music = std::move(*music);
        }

        std::vector<audio::SoundBuffer> sounds_named(const std::vector<ovl::SoundItem>& items, std::span<const std::string_view> names)
        {
            std::vector<audio::SoundBuffer> out;
            for (const auto name : names)
            {
                for (const auto& item : items)
                {
                    if (item.name != name || !item.info)
                        continue;
                    if (auto buffer = audio::from_ovl_sound(*item.info))
                        out.push_back(std::move(*buffer));
                    break;
                }
            }
            return out;
        }

        void load_clicks(MenuAudio& menu, const config::GameInstall& install)
        {
            const auto log = logging::logger("audio");
            const auto path = install.find(kClickOverlay);
            if (!path)
            {
                log.warning("no click sounds: {} not found", kClickOverlay);
                return;
            }
            const auto overlay = ovl::Overlay::load(*path);
            if (!overlay)
            {
                log.warning("no click sounds: {}: {}", kClickOverlay, ovl::to_string(overlay.error()));
                return;
            }
            const auto items = ovl::read_sounds(*overlay, overlay->common());
            menu.presses = sounds_named(items, kPressSounds);
            menu.releases = sounds_named(items, kReleaseSounds);
            menu.messages = sounds_named(items, kMessageSounds);
            if (menu.presses.empty() || menu.releases.empty())
                log.warning("click sounds missing from {}", kClickOverlay);
        }

        void play_one(audio::AudioEngine& engine, std::span<const audio::SoundBuffer> sounds, std::minstd_rand& random, float volume)
        {
            if (sounds.empty() || volume <= 0)
                return;
            std::uniform_int_distribution<std::size_t> pick(0, sounds.size() - 1);
            if (auto played = engine.play(sounds[pick(random)], { .volume = volume }); !played)
                logging::logger("audio").debug("click: {}", audio::to_string(played.error()));
        }

    } // namespace

    std::vector<std::string> music_files(const config::GameInstall& install)
    {
        std::vector<std::string> names;
        const auto folder = install.find(kMusicFolder);
        if (!folder)
            return names;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(*folder, error))
        {
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (entry.is_regular_file(error) && extension == kMusicExtension)
                names.push_back(entry.path().filename().string());
        }
        std::ranges::sort(names);
        return names;
    }

    MenuAudio start_menu_audio(audio::AudioEngine& engine, const config::GameInstall& install, config::GameOptions& options, std::uint32_t seed)
    {
        MenuAudio menu;
        menu.random.seed(seed);
        pick_cycle(menu, install, options);
        follow_choice(engine, menu, options, false);
        load_clicks(menu, install);
        return menu;
    }

    void update_menu_audio(audio::AudioEngine& engine, MenuAudio& menu, const config::GameOptions& options, std::span<const ui::InterfaceSound> sounds, bool in_park)
    {
        follow_choice(engine, menu, options, in_park);
        if (menu.music)
        {
            const float volume = audio::kMenuMusicLevel * options.get(config::GameOption::MusicVolume);
            if (volume != menu.music_volume)
            {
                menu.music->set_volume(engine, volume);
                menu.music_volume = volume;
            }
            if (auto fed = menu.music->update(engine); !fed)
                logging::logger("audio").warning("menu music: {}", fed.error());
        }
        const float clicks = options.get(config::GameOption::InterfaceVolume);
        for (const auto sound : sounds)
        {
            switch (sound)
            {
                case ui::InterfaceSound::Press:
                    play_one(engine, menu.presses, menu.random, clicks);
                    break;
                case ui::InterfaceSound::Release:
                    play_one(engine, menu.releases, menu.random, clicks);
                    break;
                case ui::InterfaceSound::Message:
                    play_one(engine, menu.messages, menu.random, clicks);
                    break;
            }
        }
    }

    void stop_menu_audio(audio::AudioEngine& engine, MenuAudio& menu)
    {
        if (menu.music)
            menu.music->stop(engine);
        menu.music.reset();
    }

} // namespace rct3::scene
