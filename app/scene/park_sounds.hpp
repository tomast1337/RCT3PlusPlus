// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's people heard from the camera, by the rules of audio/park_voices.hpp: the chatter of the crowd
// about the point looked at, a guest's cheer, laugh or retching as they show how they feel, and screams from
// the riders of a train going fast or dropping, the trains themselves by the rules of audio/ride_sounds.hpp, and
// the roar of each waterfall.
// The sounds come from the game's overlays by name; whatever it
// does not have stays quiet. All follow the game volume and hush while the park is paused, and are heard muffled while the eye is under water.
#pragma once

#include "audio/audio.hpp"
#include "audio/ride_sounds.hpp"
#include "config/game_install.hpp"
#include "simulation/crash.hpp"
#include "simulation/entt.hpp"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    /// What one train is heard making.
    struct TrainSounds
    {
        audio::TrackSoundSet set = audio::TrackSoundSet::None;
        std::array<audio::VoiceHandle, 3> running; ///< Slow, medium, fast
        audio::VoiceHandle chain;
        float chain_off = 0;      ///< Seconds since the train left the chain, while its loop fades
        bool was_running = false; ///< Last update, to hear it set off and come in
        /// The water plume loop after a splash. shortcut: never played, the track model not knowing which
        /// pieces are water; play it with the splash once it does.
        audio::VoiceHandle plume;
    };

    struct ParkSounds
    {
        std::vector<audio::SoundBuffer> chatter;
        std::vector<audio::SoundBuffer> whoops; ///< Men's cheers
        std::vector<audio::SoundBuffer> cheers; ///< Women's and children's
        std::vector<audio::SoundBuffer> laughs;
        std::array<std::vector<audio::SoundBuffer>, 3> retches;                          ///< A child's, a woman's, a man's
        std::array<std::vector<audio::SoundBuffer>, 4> screams;                          ///< A man alone, a woman alone, men, women
        std::array<audio::VoiceHandle, 2> chatter_voices;                                ///< Overlapping, so the chatter never stops
        std::set<entt::entity> heard;                                                    ///< Guests whose gesture was heard
        std::map<std::pair<entt::entity, std::size_t>, audio::VoiceHandle> train_voices; ///< By ride and train
        /// Each set's sounds, by audio::TrackSound; nothing where the set lacks one.
        std::array<std::array<std::optional<audio::SoundBuffer>, audio::kTrackSounds>, audio::kTrackSoundSets> track_sounds;
        std::map<std::pair<entt::entity, std::size_t>, TrainSounds> train_sounds; ///< By ride and train
        std::vector<glm::vec3> car_were;                                          ///< By car, where last heard
        std::vector<audio::SoundBuffer> waterfall;                                ///< The waterfalls' loops
        std::vector<audio::VoiceHandle> waterfall_voices;                         ///< By waterfall of the park
        std::vector<audio::SoundBuffer> explosions;                               ///< Of crashed trains' wrecks
        std::vector<audio::SoundBuffer> burning;                                  ///< Loops of their fires
        /// Of each wreck, by ride, train and car: its landings and state when last heard.
        std::map<std::tuple<entt::entity, std::size_t, std::size_t>, std::pair<int, simulation::WreckState>> wrecks_heard;
        std::map<entt::entity, audio::VoiceHandle> fires;      ///< By ride: the loop of its burning wrecks
        std::array<std::optional<audio::SoundBuffer>, 3> rain; ///< Loops of light rain, heavy rain and a storm
        std::array<audio::VoiceHandle, 3> rain_voices;
        std::vector<audio::SoundBuffer> thunder;
        std::minstd_rand random;
    };

    [[nodiscard]] ParkSounds load_park_sounds(const config::GameInstall& install, std::uint32_t seed);
    /// Plays what is heard in `scene` now, `elapsed` seconds after the last update.
    void update_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds, const ParkScene& scene, float elapsed);
    void stop_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds);

} // namespace rct3::scene
