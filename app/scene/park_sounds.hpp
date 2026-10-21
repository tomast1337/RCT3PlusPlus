// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's people heard from the camera, by the rules of audio/park_voices.hpp: the chatter of the crowd
// about the point looked at, a guest's cheer, laugh or retching as they show how they feel, and screams from
// the riders of a train going fast or dropping. The sounds come from the game's overlays by name; whatever it
// does not have stays quiet. All follow the game volume and hush while the park is paused.
#pragma once

#include "audio/audio.hpp"
#include "config/game_install.hpp"
#include "simulation/entt.hpp"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

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
        std::vector<glm::vec3> car_were;                                                 ///< By car, where last heard
        std::minstd_rand random;
    };

    [[nodiscard]] ParkSounds load_park_sounds(const config::GameInstall& install, std::uint32_t seed);
    /// Plays what is heard in `scene` now, `elapsed` seconds after the last update, at `volume` (0..1).
    void update_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds, const ParkScene& scene, float volume, float elapsed);
    void stop_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds);

} // namespace rct3::scene
