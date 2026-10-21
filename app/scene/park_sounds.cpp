// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_sounds.hpp"

#include "audio/park_voices.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "park/camera.hpp"
#include "scene/menu_audio.hpp"
#include "scene/park_scene.hpp"
#include "simulation/components.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <span>
#include <string_view>

namespace rct3::scene
{

    namespace
    {

        constexpr float kChatterReach = 30;   ///< World units about the camera's point whose people are heard chatting
        constexpr float kChatterLevel = 0.5f; ///< Of the game volume, with a full crowd
        constexpr float kGestureLevel = 0.8f;
        constexpr float kScreamLevel = 0.9f;

        std::vector<audio::SoundBuffer> load_named(const config::GameInstall& install, std::string_view file, std::span<const std::string_view> names)
        {
            const auto path = install.find(file);
            if (!path)
                return { };
            const auto overlay = ovl::Overlay::load(*path);
            if (!overlay)
            {
                logging::logger("audio").warning("no park sounds from {}: {}", file, ovl::to_string(overlay.error()));
                return { };
            }
            return sounds_named(ovl::read_sounds(*overlay, overlay->common()), names);
        }

        audio::VoiceHandle play_any(audio::AudioEngine& engine, std::span<const audio::SoundBuffer> sounds, std::minstd_rand& random, audio::PlayParams params)
        {
            if (sounds.empty() || params.volume <= 0)
                return { };
            std::uniform_int_distribution<std::size_t> pick(0, sounds.size() - 1);
            std::uniform_real_distribution<float> pitch(0.94f, 1.06f);
            params.pitch *= pitch(random);
            const auto played = engine.play(sounds[pick(random)], params);
            return played ? *played : audio::VoiceHandle { };
        }

    } // namespace

    ParkSounds load_park_sounds(const config::GameInstall& install, std::uint32_t seed)
    {
        ParkSounds s;
        s.random.seed(seed);
        // Each of the main overlay's sounds by its name there.
        const auto main = [&](std::span<const std::string_view> names) { return load_named(install, "Main.common.ovl", names); };
        constexpr std::array<std::string_view, 3> kChatter { "CrowdMurmur1", "CrowdMurmur2", "CrowdMurmur3" };
        s.chatter = main(kChatter);
        constexpr std::array<std::string_view, 2> kWhoops { "CrashPeepsYeeHaa1x", "CrashPeepsYeeHaa2x" };
        s.whoops = main(kWhoops);
        constexpr std::array<std::string_view, 2> kCheers { "CrashPeepsYeaaah1x", "CrashPeepsWeee1x" };
        s.cheers = main(kCheers);
        constexpr std::array<std::string_view, 1> kLaughs { "CrashPeepsLaugh1x" };
        s.laughs = main(kLaughs);
        constexpr std::array<std::array<std::string_view, 2>, 3> kRetches { { { "KidVomit1", "KidVomit2" }, { "FemaleVomit1", "FemaleVomit2" }, { "MaleVomit1", "MaleVomit2" } } };
        for (std::size_t i = 0; i < kRetches.size(); ++i)
            s.retches[i] = main(kRetches[i]);
        constexpr std::array<std::array<std::string_view, 3>, 4> kScreams { { { "ScreamBoy1", "ScreamBoy2" }, { "ScreamGirl1", "ScreamGirl2" }, { "ScreamBoys1", "ScreamBoys2", "ScreamBoys3" }, { "ScreamGirls1", "ScreamGirls2", "ScreamGirls3" } } };
        if (const auto path = install.find("Sounds/Sounds.common.ovl"))
            if (const auto overlay = ovl::Overlay::load(*path))
            {
                const auto items = ovl::read_sounds(*overlay, overlay->common());
                for (std::size_t i = 0; i < kScreams.size(); ++i)
                    s.screams[i] = sounds_named(items, kScreams[i]);
            }
        return s;
    }

    void update_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds, const ParkScene& scene, float volume, float elapsed)
    {
        const auto& r = scene.sim.registry;
        const bool quiet = scene.park.speed.paused || !scene.simulate || volume <= 0;
        const glm::vec3 eye = park::eye(scene.camera);
        engine.set_listener(eye, glm::normalize(scene.camera.target - eye), glm::vec3(0.0f, 1.0f, 0.0f));
        engine.set_muffled(scene.water.eye_in.has_value());

        // The chatter: two clips overlapping, each started again with another when it ends, heard from where the
        // people about the camera's point stand, however far the eye.
        std::size_t near = 0;
        glm::vec3 crowd { 0.0f };
        if (!quiet)
            for (const auto [e, pose, errand] : r.view<const simulation::Pose, const simulation::Errand>(entt::exclude<simulation::Left>).each())
                if (errand.kind != simulation::ErrandKind::Riding && glm::distance(glm::vec2(pose.position.x, pose.position.z), glm::vec2(scene.camera.target.x, scene.camera.target.z)) <= kChatterReach)
                {
                    ++near;
                    crowd += pose.position;
                }
        crowd = near > 0 ? crowd / static_cast<float>(near) : scene.camera.target;
        const float chatter = quiet ? 0.0f : volume * kChatterLevel * audio::chatter_level(near);
        for (audio::VoiceHandle& voice : sounds.chatter_voices)
        {
            if (!engine.is_playing(voice))
                voice = play_any(engine, sounds.chatter, sounds.random, { .volume = chatter, .at = crowd, .near = audio::kFarVoice, .far = 2 * audio::kFarVoice });
            else
            {
                engine.set_voice_volume(voice, chatter);
                engine.set_voice_place(voice, crowd);
            }
        }
        if (quiet)
            return;

        // Guests showing how they feel, heard as they start.
        std::set<entt::entity> heard;
        for (const auto [e, gesture, pose, visitor] : r.view<const simulation::Gesture, const simulation::Pose, const simulation::Visitor>().each())
        {
            heard.insert(e);
            if (sounds.heard.contains(e))
                continue;
            const bool man = !visitor.female && visitor.age != simulation::AgeGroup::Child;
            const audio::PlayParams params = audio::heard(pose.position, volume * kGestureLevel);
            using enum simulation::GestureKind;
            if (gesture.kind == Cheer)
                play_any(engine, man ? sounds.whoops : sounds.cheers, sounds.random, params);
            else if (gesture.kind == Laugh)
                play_any(engine, sounds.laughs, sounds.random, params);
            else if (gesture.kind == Retch)
                play_any(engine, sounds.retches[visitor.age == simulation::AgeGroup::Child ? 0 : visitor.female ? 1 : 2], sounds.random, params);
        }
        sounds.heard = std::move(heard);

        // Riders screaming, one voice a train at most, from its first car.
        if (sounds.car_were.size() != scene.car_places.size() || elapsed <= 0)
        {
            sounds.car_were.clear();
            for (const glm::mat4& place : scene.car_places)
                sounds.car_were.emplace_back(place[3]);
        }
        std::set<std::pair<entt::entity, std::size_t>> trains;
        for (std::size_t i = 0; i < scene.car_places.size() && i < scene.car_spots.size(); ++i)
        {
            const glm::vec3 at { scene.car_places[i][3] };
            const glm::vec3 velocity = elapsed > 0 ? (at - sounds.car_were[i]) / elapsed : glm::vec3 { 0.0f };
            sounds.car_were[i] = at;
            const park::CarSpot spot = scene.car_spots[i];
            const entt::entity ride = spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
            const auto* fleet = ride != entt::null ? r.try_get<simulation::Trains>(ride) : nullptr;
            if (fleet == nullptr || spot.train >= fleet->trains.size() || !trains.emplace(ride, spot.train).second)
                continue;
            const std::vector<entt::entity>& riders = fleet->riders[spot.train];
            audio::VoiceHandle& voice = sounds.train_voices[{ ride, spot.train }];
            if (engine.is_playing(voice))
                engine.set_voice_place(voice, at, velocity);
            if (riders.empty() || !fleet->trains[spot.train].running || engine.is_playing(voice))
                continue;
            std::bernoulli_distribution scream(std::min(audio::screams_per_second(fleet->trains[spot.train].speed, -velocity.y) * elapsed, 1.0f));
            if (!scream(sounds.random))
                continue;
            // A rider picked at random leads, joined by the rest when there are a few.
            const entt::entity lead = riders[std::uniform_int_distribution<std::size_t>(0, riders.size() - 1)(sounds.random)];
            const auto* visitor = r.try_get<simulation::Visitor>(lead);
            const std::size_t woman = visitor != nullptr && visitor->female ? 1 : 0;
            voice = play_any(engine, sounds.screams[(riders.size() >= 3 ? 2 : 0) + woman], sounds.random, audio::heard(at, volume * kScreamLevel));
            engine.set_voice_place(voice, at, velocity);
        }
    }

    void stop_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds)
    {
        for (const audio::VoiceHandle voice : sounds.chatter_voices)
            engine.stop(voice);
        for (const auto& [_, voice] : sounds.train_voices)
            engine.stop(voice);
    }

} // namespace rct3::scene
