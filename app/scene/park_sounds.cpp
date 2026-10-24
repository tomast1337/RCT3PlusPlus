// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_sounds.hpp"

#include "audio/park_voices.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "park/camera.hpp"
#include "park/waterfall_mesh.hpp"
#include "scene/menu_audio.hpp"
#include "scene/park_scene.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"

#include <algorithm>
#include <format>
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
        constexpr float kRideLevel = 0.7f;
        constexpr float kChainFade = 0.5f;      ///< Seconds the chain's loop takes to come in and to die away
        constexpr float kWaterfallLevel = 0.6f; ///< Of the game volume.
        constexpr float kWaterfallNear = 10;    ///< World units from where it lands a waterfall is heard full loud.
        constexpr float kWaterfallFar = 90;     ///< And no longer heard.

        void silence_trains(audio::AudioEngine& engine, ParkSounds& sounds)
        {
            for (const auto& [_, train] : sounds.train_sounds)
            {
                for (const audio::VoiceHandle voice : train.running)
                    engine.stop(voice);
                engine.stop(train.chain);
                engine.stop(train.plume);
            }
            sounds.train_sounds.clear();
        }

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
        constexpr std::array<std::string_view, 2> kWaterfall { "Waterfall3", "Waterfall4" };
        s.waterfall = load_named(install, "Sounds/Waterfall/Waterfall.common.ovl", kWaterfall);
        for (std::size_t set = 1; set < audio::kTrackSoundSets; ++set)
        {
            const auto path = install.find(std::format("tracks/TrackSounds/{}.common.ovl", audio::file_of(static_cast<audio::TrackSoundSet>(set))));
            if (!path)
                continue;
            const auto overlay = ovl::Overlay::load(*path);
            if (!overlay)
                continue;
            const auto items = ovl::read_sounds(*overlay, overlay->common());
            for (std::size_t sound = 0; sound < audio::kTrackSounds; ++sound)
            {
                const std::string_view name = audio::name_of(static_cast<audio::TrackSound>(sound));
                if (auto found = sounds_named(items, std::span(&name, 1)); !found.empty())
                    s.track_sounds[set][sound] = std::move(found.front());
            }
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
        // Each waterfall's roar, a loop heard from where its water lands; the falls take the clips in turn.
        const auto& falls = scene.footpaths.waterfalls;
        for (std::size_t i = falls.size(); i < sounds.waterfall_voices.size(); ++i)
            engine.stop(sounds.waterfall_voices[i]);
        sounds.waterfall_voices.resize(falls.size());
        for (std::size_t i = 0; i < falls.size(); ++i)
        {
            audio::VoiceHandle& voice = sounds.waterfall_voices[i];
            if (quiet || sounds.waterfall.empty())
            {
                engine.stop(voice);
                voice = { };
                continue;
            }
            const glm::vec3 at = park::waterfall_point(falls[i], 1.0f);
            if (!engine.is_playing(voice))
            {
                const auto played = engine.play(sounds.waterfall[i % sounds.waterfall.size()], { .volume = volume * kWaterfallLevel, .at = at, .near = kWaterfallNear, .far = kWaterfallFar, .loop = true });
                voice = played ? *played : audio::VoiceHandle { };
            }
            else
            {
                engine.set_voice_volume(voice, volume * kWaterfallLevel);
                engine.set_voice_place(voice, at);
            }
        }
        if (quiet)
        {
            silence_trains(engine, sounds);
            return;
        }

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
        struct HeardTrain
        {
            std::pair<entt::entity, std::size_t> key;
            std::size_t ride = 0; ///< In scene.rides
            glm::vec3 at { }, velocity { };
            const simulation::Coaster* train = nullptr;
        };
        std::vector<HeardTrain> heard_trains;
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
            heard_trains.push_back({ { ride, spot.train }, spot.ride, at, velocity, &fleet->trains[spot.train] });
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

        // The trains themselves, the nearest few: running loops crossing over with speed, the chain while on a
        // lift, and a sound as each sets off and comes in.
        std::ranges::sort(heard_trains, { }, [&](const HeardTrain& t) { return glm::distance(t.at, eye); });
        std::map<std::pair<entt::entity, std::size_t>, TrainSounds> kept;
        for (std::size_t i = 0; i < heard_trains.size(); ++i)
        {
            const HeardTrain& t = heard_trains[i];
            auto node = sounds.train_sounds.extract(t.key);
            TrainSounds train = node ? std::move(node.mapped()) : TrainSounds { .set = t.ride < scene.rides.rides.size() ? audio::track_sound_set(scene.rides.rides[t.ride].base.overlay_path) : audio::TrackSoundSet::None, .was_running = t.train->running };
            const auto& kit = sounds.track_sounds[static_cast<std::size_t>(train.set)];
            const float level = i < audio::kHeardTrains ? volume * kRideLevel : 0.0f;
            const auto params = [&](float gain) { return audio::PlayParams { .volume = gain, .at = t.at, .near = audio::kRideNear, .far = audio::kRideFar }; };
            const auto sound = [&](audio::TrackSound which) -> const std::optional<audio::SoundBuffer>& { return kit[static_cast<std::size_t>(which)]; };

            using enum audio::TrackSound;
            if (t.train->running != train.was_running && level > 0)
                if (const auto& shot = sound(t.train->running ? Start : Stop))
                    (void)engine.play(*shot, params(level));
            train.was_running = t.train->running;

            // A loop at `gain`, started when it is heard and stopped when it is not.
            const auto keep = [&](audio::VoiceHandle& voice, const std::optional<audio::SoundBuffer>& loop, float gain) {
                if (gain <= 0 || !loop)
                {
                    engine.stop(voice);
                    voice = { };
                    return;
                }
                if (!engine.is_playing(voice))
                {
                    audio::PlayParams p = params(gain);
                    p.loop = true;
                    const auto played = engine.play(*loop, p);
                    voice = played ? *played : audio::VoiceHandle { };
                }
                engine.set_voice_volume(voice, gain);
                engine.set_voice_place(voice, t.at, t.velocity);
            };
            const audio::RunningMix mix = audio::running_mix(t.train->running ? t.train->speed : 0.0f, sound(Med).has_value(), sound(Fast).has_value());
            keep(train.running[0], sound(Slow), level * mix.slow);
            keep(train.running[1], sound(Med), level * mix.med);
            keep(train.running[2], sound(Fast), level * mix.fast);

            // The chain comes in as the train reaches the lift and dies away once all of it is off.
            const bool chained = t.train->running && simulation::on_chain(*t.train);
            train.chain_off = chained ? 0.0f : train.chain_off + elapsed;
            const bool was_playing = engine.is_playing(train.chain);
            keep(train.chain, sound(Chain), chained || (was_playing && train.chain_off < kChainFade) ? level : 0.0f);
            if (chained && !was_playing && engine.is_playing(train.chain))
            {
                engine.fade_voice(train.chain, 0, 0);
                engine.fade_voice(train.chain, 1, kChainFade);
            }
            else if (!chained)
                engine.fade_voice(train.chain, 0, std::max(kChainFade - train.chain_off, 0.0f));

            // Water: a splash as the front enters water, by its speed, then the WaterPlume loop on `train.plume`
            // for a few seconds. Nothing yet: the track model does not know which pieces are water.
            kept.emplace(t.key, std::move(train));
        }
        silence_trains(engine, sounds); // Trains no longer there
        sounds.train_sounds = std::move(kept);
    }

    void stop_park_sounds(audio::AudioEngine& engine, ParkSounds& sounds)
    {
        for (const audio::VoiceHandle voice : sounds.chatter_voices)
            engine.stop(voice);
        for (const auto& [_, voice] : sounds.train_voices)
            engine.stop(voice);
        for (const audio::VoiceHandle voice : sounds.waterfall_voices)
            engine.stop(voice);
        sounds.waterfall_voices.clear();
        silence_trains(engine, sounds);
    }

} // namespace rct3::scene
