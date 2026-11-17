// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/fireworks.hpp"

#include "logging/log.hpp"
#include "park/object/mesh.hpp"
#include "save/save_file.hpp"
#include "scene/park/scene.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/messages.hpp"

#include <algorithm>

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

        void fire(ParkScene& scene, const particles::Display& display, const particles::Shot& shot)
        {
            const auto i = launcher_object(scene, display.launchers[shot.launcher]);
            if (!i)
                return;
            const rct3::park::object::ObjectModel* model = *i < scene.places.models.size() ? scene.places.models[*i] : nullptr;
            const rct3::park::object::ObjectModel none { };
            const rct3::park::object::PlacedEffect from = rct3::park::object::launch_point(object_place(scene, *i), model != nullptr ? *model : none, static_cast<std::size_t>(std::max(shot.tube, 0)));
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
        if (!fireworks.playing)
            return;
        ParkFireworks::Playing& playing = *fireworks.playing;
        const particles::Display& display = fireworks.displays[playing.display];
        for (const particles::Shot& shot : particles::advance_display(playing.run, display, park_time.value))
            fire(scene, display, shot);
        const float last = display.shots.empty() ? 0.0f : display.shots.back().seconds;
        if (playing.run.next_shot >= display.shots.size() && playing.run.seconds > last + kAfterLastShot.value)
            fireworks.playing.reset();
    }

} // namespace rct3::scene::park
