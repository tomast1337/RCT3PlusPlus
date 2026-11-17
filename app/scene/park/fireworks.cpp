// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/fireworks.hpp"

#include "logging/log.hpp"
#include "park/object/mesh.hpp"
#include "save/save_file.hpp"
#include "scene/park/scene.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/messages.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::scene::park
{

    namespace
    {
        /// Chosen by eye: how long a display runs on after its last shot, while the last bursts fade.
        constexpr units::Seconds kAfterLastShot { 8 };
        /// Chosen by eye: how high over its launchers a display's view takes in, and how far down it looks.
        constexpr units::Distance kShowHeight { 60 };
        constexpr units::Radians kShowPitch { 0.2f };
        constexpr float kMinutesPerDay = simulation::kHoursPerDay * 60;
        constexpr units::Distance kJetRadius { 0.06f }; ///< Chosen by eye: a show jet's stream at the nozzle, thinner than a cannon's
        constexpr float kJetBreakup = 0.7f;             ///< Chosen by eye: how much it breaks into spray as it falls
        constexpr units::Seconds kJetLinger { 0.5f };   ///< Its drops live this much past coming down to the nozzle's height

        /// Where launcher `launcher` of the display stands, by its place in the park's objects; none once it is gone.
        std::optional<std::size_t> launcher_object(const ParkScene& scene, const particles::Launcher& launcher)
        {
            if (!launcher.placed)
                return std::nullopt;
            const auto o = std::ranges::find(scene.objects, *launcher.placed, &save::model::PlacedObject::id);
            if (o == scene.objects.end())
                return std::nullopt;
            return static_cast<std::size_t>(o - scene.objects.begin());
        }

        glm::mat4 object_place(const ParkScene& scene, std::size_t i)
        {
            if (i < scene.places.transforms.size())
                return scene.places.transforms[i];
            const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects[i]));
            return rct3::park::object::placed_transform(scene.objects[i], model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted);
        }

        /// Where launcher object `i` shoots from by tube `tube`, pointing the way the tube does.
        rct3::park::object::PlacedEffect tube_of(const ParkScene& scene, std::size_t i, std::size_t tube)
        {
            const rct3::park::object::ObjectModel* model = i < scene.places.models.size() ? scene.places.models[i] : nullptr;
            const rct3::park::object::ObjectModel none { };
            return rct3::park::object::launch_point(object_place(scene, i), model != nullptr ? *model : none, tube);
        }

        /// A curve's value, 0 when it has no points.
        float value_at(const particles::Curve& curve, float t)
        {
            return curve.values.empty() ? 0.0f : curve.at(t);
        }

        void fire(ParkScene& scene, const particles::Display& display, const particles::Shot& shot)
        {
            const auto i = launcher_object(scene, display.launchers[shot.launcher]);
            if (!i)
                return;
            const auto tube = static_cast<std::size_t>(std::max(shot.tube, 0));
            // shortcut: laser shots are not drawn, draw them once the park draws laser beams.
            if (shot.kind == particles::ShotKind::Laser)
                return;
            if (shot.kind == particles::ShotKind::WaterJet)
            {
                if (!shot.jet || shot.jet->seconds <= 0)
                    return;
                const auto& speeds = shot.jet->pressure.values;
                // Its drops live long enough to come down from the highest it shoots, and a little to splash.
                const float highest = speeds.empty() ? 0.0f : std::ranges::max(speeds);
                const float life = 2 * highest / units::kGravity.value + kJetLinger.value;
                // The pattern's widening, near 1, is read as what the stream's width is multiplied by each second.
                const float spread = kJetRadius.value * std::max(value_at(shot.jet->widening, 0) - 1, 0.0f);
                scene.jets.push_back(ParkScene::Jet { .object = save::model::PlacedObjectIndex { *i }, .water = rct3::park::water::WaterJet(life, kJetRadius.value, spread, kJetBreakup), .pattern = shot.jet, .shot_at = scene.park_clock, .tube = tube });
                return;
            }
            const rct3::park::object::PlacedEffect from = tube_of(scene, *i, tube);
            if (!fire_park_particles(scene.particles, shot.effect, from.position, from.axes[1]))
                logging::logger("fireworks").debug("no firework effect {}", shot.effect);
        }

    } // namespace

    void load_park_fireworks(ParkFireworks& fireworks, const save::SaveFile& file)
    {
        fireworks = { };
        auto displays = particles::read_park_displays(file);
        if (!displays)
        {
            logging::logger("fireworks").warning("the park's firework displays: {}", particles::to_string(displays.error()));
            return;
        }
        fireworks.displays = std::move(*displays);
        fireworks.was_due.assign(fireworks.displays.size(), 0);
    }

    bool start_park_display(ParkScene& scene, std::size_t display)
    {
        ParkFireworks& fireworks = scene.fireworks;
        if (display >= fireworks.displays.size())
            return false;
        const particles::Display& d = fireworks.displays[display];
        glm::vec3 sum { 0.0f };
        std::size_t standing = 0;
        for (const particles::Launcher& l : d.launchers)
            if (const auto i = launcher_object(scene, l))
            {
                sum += glm::vec3(object_place(scene, *i)[3]);
                ++standing;
            }
        fireworks.playing = ParkFireworks::Playing { .display = display, .run = { }, .centre = standing > 0 ? sum / static_cast<float>(standing) : glm::vec3(0.0f) };
        simulation::park::post(scene.park.messages, { .text_key = std::string(kDisplayMessage), .subject_name = d.name });
        logging::logger("fireworks").info("display {} starts, {} shots from {} launchers", d.name, d.shots.size(), standing);
        return true;
    }

    std::optional<rct3::park::ParkCamera> park_display_view(const ParkScene& scene, float yaw)
    {
        if (!scene.fireworks.playing)
            return std::nullopt;
        std::vector<glm::vec3> points;
        for (const particles::Launcher& l : scene.fireworks.displays[scene.fireworks.playing->display].launchers)
            if (const auto i = launcher_object(scene, l))
            {
                const glm::vec3 at = object_place(scene, *i)[3];
                points.push_back(at);
                points.push_back(at + glm::vec3(0, kShowHeight.value, 0));
            }
        if (points.empty())
            return std::nullopt;
        return rct3::park::framed(points, yaw, kShowPitch.value);
    }

    void stop_park_display(ParkScene& scene)
    {
        scene.fireworks.playing.reset();
    }

    void update_park_fireworks(ParkScene& scene, units::Seconds park_time)
    {
        ParkFireworks& fireworks = scene.fireworks;
        const float minutes = scene.day_clock.time * kMinutesPerDay;
        for (std::size_t i = 0; i < fireworks.displays.size(); ++i)
        {
            const bool due = particles::display_due(fireworks.displays[i], scene.park.date.month, minutes);
            if (due && fireworks.was_due[i] == 0 && !fireworks.playing)
                start_park_display(scene, i);
            fireworks.was_due[i] = due ? 1 : 0;
        }
        if (fireworks.playing)
        {
            ParkFireworks::Playing& playing = *fireworks.playing;
            const particles::Display& display = fireworks.displays[playing.display];
            for (const particles::Shot& shot : particles::advance_display(playing.run, display, park_time.value))
                fire(scene, display, shot);
            if (playing.run.next_shot >= display.shots.size() && playing.run.seconds > particles::display_seconds(display) + kAfterLastShot.value)
                fireworks.playing.reset();
        }
        // The show's jets shoot as their patterns say, each from its start; past its end one is let be, and stops.
        for (ParkScene::Jet& jet : scene.jets)
        {
            const float into = scene.park_clock - jet.shot_at;
            if (!jet.pattern || into >= jet.pattern->seconds || jet.object.value >= scene.objects.size())
                continue;
            const float t = into / jet.pattern->seconds;
            const rct3::park::object::PlacedEffect from = tube_of(scene, jet.object.value, jet.tube);
            const glm::vec3 up = from.axes[1];
            // Its lean, in radians, tips it toward the launcher's front, and back below 0: a guess, as the files do not
            // say which way it leans.
            const glm::vec3 ahead = glm::mat3(object_place(scene, jet.object.value)) * glm::vec3(0, 0, 1);
            const glm::vec3 across = ahead - glm::dot(ahead, up) * up;
            const glm::vec3 front = glm::length(across) > 1e-4f ? glm::normalize(across) : from.axes[2];
            const float lean = value_at(jet.pattern->elevation, t);
            // Its pressure is read as the speed the water leaves at, meters a second: a common pressure of 10 then
            // rises five meters, about as high as the jets look in the game.
            jet.water.aim(from.position, value_at(jet.pattern->pressure, t) * (std::cos(lean) * up + std::sin(lean) * front));
            jet.fed = true;
        }
    }

} // namespace rct3::scene::park
