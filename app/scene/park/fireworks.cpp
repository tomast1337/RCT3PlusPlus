// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/fireworks.hpp"

#include "core/types.hpp"
#include "logging/log.hpp"
#include "park/object/mesh.hpp"
#include "save/save_file.hpp"
#include "scene/park/scene.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace rct3::scene::park
{

    namespace
    {
        /// Chosen by eye: how long a display runs on after its last shot, while the last bursts fade.
        constexpr units::Seconds kAfterLastShot { 8 };
        /// Chosen by eye: how high over its launchers a display's view takes in, and how far down it looks.
        constexpr units::Distance kShowHeight { 60 };
        constexpr units::Radians kShowPitch { 0.2f };
        constexpr f32 kMinutesPerDay = simulation::kHoursPerDay * 60;
        constexpr units::Distance kJetRadius { 0.06f }; ///< Chosen by eye: a show jet's stream at the nozzle, thinner than a cannon's
        constexpr f32 kJetBreakup = 0.7f;               ///< Chosen by eye: how much it breaks into spray as it falls
        constexpr units::Seconds kJetLinger { 0.5f };   ///< Its drops live this much past coming down to the nozzle's height

        /// Where launcher `launcher` of the display stands, by its place in the park's objects; none once it is gone.
        std::optional<usize> launcher_object(const ParkScene& scene, const particles::Launcher& launcher)
        {
            if (!launcher.placed)
                return std::nullopt;
            const auto o = std::ranges::find(scene.objects, *launcher.placed, &save::model::PlacedObject::id);
            if (o == scene.objects.end())
                return std::nullopt;
            return static_cast<usize>(o - scene.objects.begin());
        }

        glm::mat4 object_place(const ParkScene& scene, usize i)
        {
            if (i < scene.places.transforms.size())
                return scene.places.transforms[i];
            const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects[i]));
            return rct3::park::object::placed_transform(scene.objects[i], model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted);
        }

        /// Where launcher object `i` shoots from by tube `tube`, pointing the way the tube does.
        rct3::park::object::PlacedEffect tube_of(const ParkScene& scene, usize i, usize tube)
        {
            const rct3::park::object::ObjectModel* model = i < scene.places.models.size() ? scene.places.models[i] : nullptr;
            const rct3::park::object::ObjectModel none { };
            return rct3::park::object::launch_point(object_place(scene, i), model != nullptr ? *model : none, tube);
        }

        /// A curve's value, 0 when it has no points.
        f32 value_at(const particles::Curve& curve, f32 t)
        {
            return curve.values.empty() ? 0.0f : curve.at(t);
        }

        void fire(ParkScene& scene, const particles::Display& display, const particles::Shot& shot)
        {
            const auto i = launcher_object(scene, display.launchers[shot.launcher]);
            if (!i)
                return;
            const auto tube = static_cast<usize>(std::max(shot.tube, 0));
            if (shot.kind == particles::ShotKind::Laser)
            {
                if (shot.laser && shot.laser->seconds > 0)
                    scene.fireworks.lasers.push_back({ .shot = static_cast<usize>(&shot - display.shots.data()), .object = save::model::PlacedObjectIndex { *i }, .tube = tube, .shot_at = scene.park_clock });
                return;
            }
            if (shot.kind == particles::ShotKind::WaterJet)
            {
                if (!shot.jet || shot.jet->seconds <= 0)
                    return;
                const auto& speeds = shot.jet->pressure.values;
                // Its drops live long enough to come down from the highest it shoots, and a little to splash.
                const f32 highest = speeds.empty() ? 0.0f : std::ranges::max(speeds);
                const f32 life = 2 * highest / units::kGravity.value + kJetLinger.value;
                // The pattern's widening, near 1, is read as what the stream's width is multiplied by each second.
                const f32 spread = kJetRadius.value * std::max(value_at(shot.jet->widening, 0) - 1, 0.0f);
                scene.jets.push_back(ParkScene::Jet { .object = save::model::PlacedObjectIndex { *i }, .water = rct3::park::water::WaterJet(life, kJetRadius.value, spread, kJetBreakup), .pattern = shot.jet, .shot_at = scene.park_clock, .tube = tube });
                return;
            }
            const rct3::park::object::PlacedEffect from = tube_of(scene, *i, tube);
            if (!fire_park_particles(scene.particles, shot.effect, from.position, from.axes[1]))
                logging::logger("fireworks").debug("no firework effect {}", shot.effect);
        }

    } // namespace

    void load_park_fireworks(render::Device& device, ParkFireworks& fireworks, const save::SaveFile& file)
    {
        destroy_park_fireworks(device, fireworks);
        constexpr std::array kAttributes {
            render::VertexAttribute { 0, render::VertexFormat::Float3, offsetof(ParkFireworks::BeamCorner, position) },
            render::VertexAttribute { 1, render::VertexFormat::Float4, offsetof(ParkFireworks::BeamCorner, colour) },
        };
        if (auto made = device.create_pipeline({ .shader = render::Shader::VertexColor, .layout = { sizeof(ParkFireworks::BeamCorner), kAttributes }, .blend = render::BlendMode::Add, .depth_write = false, .label = "laser beams" }))
            fireworks.beams = *made;
        else
            logging::logger("fireworks").warning("laser beams: {}", made.error());
        auto displays = particles::read_park_displays(file);
        if (!displays)
        {
            logging::logger("fireworks").warning("the park's firework displays: {}", particles::to_string(displays.error()));
            return;
        }
        fireworks.displays = std::move(*displays);
        fireworks.was_due.assign(fireworks.displays.size(), 0);
    }

    void destroy_park_fireworks(render::Device& device, ParkFireworks& fireworks)
    {
        if (fireworks.beams)
            device.destroy_pipeline(fireworks.beams);
        fireworks = { };
    }

    bool start_park_display(ParkScene& scene, usize display)
    {
        ParkFireworks& fireworks = scene.fireworks;
        if (display >= fireworks.displays.size())
            return false;
        fireworks.lasers.clear();
        const particles::Display& d = fireworks.displays[display];
        glm::vec3 sum { 0.0f };
        usize standing = 0;
        for (const particles::Launcher& l : d.launchers)
            if (const auto i = launcher_object(scene, l))
            {
                sum += glm::vec3(object_place(scene, *i)[3]);
                ++standing;
            }
        fireworks.playing = ParkFireworks::Playing { .display = display, .run = { }, .centre = standing > 0 ? sum / static_cast<f32>(standing) : glm::vec3(0.0f), .ticks_left = simulation::ticks(particles::display_seconds(d) + kAfterLastShot.value) };
        simulation::park::post(scene.park.messages, { .text_key = std::string(kDisplayMessage), .subject_name = d.name });
        logging::logger("fireworks").info("display {} starts, {} shots from {} launchers", d.name, d.shots.size(), standing);
        return true;
    }

    std::optional<rct3::park::ParkCamera> park_display_view(const ParkScene& scene, f32 yaw)
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
        scene.fireworks.lasers.clear();
    }

    void tick_park_fireworks(ParkScene& scene)
    {
        ParkFireworks& fireworks = scene.fireworks;
        if (fireworks.playing && --fireworks.playing->ticks_left == 0)
            stop_park_display(scene);
        const f32 minutes = scene.day_clock.time * kMinutesPerDay;
        for (usize i = 0; i < fireworks.displays.size(); ++i)
        {
            const bool due = particles::display_due(fireworks.displays[i], scene.park.date.month, minutes);
            if (due && fireworks.was_due[i] == 0 && !fireworks.playing)
                start_park_display(scene, i);
            fireworks.was_due[i] = due ? 1 : 0;
        }
    }

    void update_park_fireworks(ParkScene& scene, units::Seconds park_time)
    {
        ParkFireworks& fireworks = scene.fireworks;
        if (fireworks.playing)
        {
            const particles::Display& display = fireworks.displays[fireworks.playing->display];
            for (const particles::Shot& shot : particles::advance_display(fireworks.playing->run, display, park_time.value))
                fire(scene, display, shot);
            std::erase_if(fireworks.lasers, [&](const ParkFireworks::Shining& l) { return scene.park_clock - l.shot_at >= display.shots[l.shot].laser->seconds; });
        }
        // The show's jets shoot as their patterns say, each from its start; past its end one is let be, and stops.
        for (ParkScene::Jet& jet : scene.jets)
        {
            const f32 into = scene.park_clock - jet.shot_at;
            if (!jet.pattern || into >= jet.pattern->seconds || jet.object.value >= scene.objects.size())
                continue;
            const f32 t = into / jet.pattern->seconds;
            const rct3::park::object::PlacedEffect from = tube_of(scene, jet.object.value, jet.tube);
            const glm::vec3 up = from.axes[1];
            // Its lean, in radians, tips it toward the launcher's front, and back below 0: a guess, as the files do not
            // say which way it leans.
            const glm::vec3 ahead = glm::mat3(object_place(scene, jet.object.value)) * glm::vec3(0, 0, 1);
            const glm::vec3 across = ahead - glm::dot(ahead, up) * up;
            const glm::vec3 front = glm::length(across) > 1e-4f ? glm::normalize(across) : from.axes[2];
            const f32 lean = value_at(jet.pattern->elevation, t);
            // Its pressure is read as the speed the water leaves at, meters a second: a common pressure of 10 then
            // rises five meters, about as high as the jets look in the game.
            jet.water.aim(from.position, value_at(jet.pattern->pressure, t) * (std::cos(lean) * up + std::sin(lean) * front));
            jet.fed = true;
        }
    }

    void draw_park_lasers(render::Device& device, ParkScene& scene, const glm::mat4& view_projection, glm::vec3 eye)
    {
        ParkFireworks& fireworks = scene.fireworks;
        if (!fireworks.playing || fireworks.lasers.empty() || !fireworks.beams)
            return;
        constexpr units::Distance kBeamHalfWidth { 0.06f }; ///< Chosen by eye: a beam where it leaves the tube, at a width of 1
        /// Chosen by eye: how fast a beam opens with distance for each unit its width is past 1, so a searchlight's
        /// width of about 1.6 throws a broad cone while the common 1 stays a thin line.
        constexpr f32 kOpeningPerWidth = 0.25f;
        constexpr f32 kThinnestSeen = 0.0025f; ///< Chosen by eye: the least half width, as a share of the distance from the eye, two or three pixels, so far beams do not flicker away
        constexpr f32 kFarEnd = 0.15f;         ///< Chosen by eye: how bright a beam still is at its far end, fading into the air
        constexpr f32 kDimmest = 0.04f;        ///< Chosen by eye: the least a wide beam dims to
        constexpr usize kTrailCopies = 3;      ///< Chosen by eye: the earlier beams drawn fainter behind a moving one

        const particles::Display& display = fireworks.displays[fireworks.playing->display];
        fireworks.beam_corners.clear();
        for (const ParkFireworks::Shining& shining : fireworks.lasers)
        {
            if (shining.object.value >= scene.objects.size())
                continue;
            const particles::Laser& laser = *display.shots[shining.shot].laser;
            const rct3::park::object::PlacedEffect from = tube_of(scene, shining.object.value, shining.tube);
            const f32 into = scene.park_clock - shining.shot_at;
            const f32 length = std::max(laser.power, 1.0f);
            // shortcut: a laser of the drawing look shines nothing; trace its drawings once lasers draw pictures.
            for (usize copy = 0; copy <= kTrailCopies && (copy == 0 || laser.trail_seconds > 0); ++copy)
            {
                const f32 at = into - laser.trail_seconds * static_cast<f32>(copy) / kTrailCopies;
                if (at < 0)
                    break;
                const f32 t = at / laser.seconds;
                const f32 width = std::max(laser.width.at(t), 0.0f);
                const f32 near = kBeamHalfWidth.value * width;
                const f32 far = near + length * kOpeningPerWidth * std::max(width - 1, 0.0f);
                // A wide beam spreads its light over its width, so many wide beams do not wash the view white.
                const f32 dim = std::clamp(std::sqrt(2 * kBeamHalfWidth.value / std::max(near + far, 1e-4f)), kDimmest, 1.0f);
                const f32 fade = 1 - static_cast<f32>(copy) / (kTrailCopies + 1);
                const f32 opacity = laser.colour[3].at(t);
                const glm::vec3 light = glm::vec3(laser.colour[0].at(t), laser.colour[1].at(t), laser.colour[2].at(t)) * opacity * dim * fade;
                if (glm::all(glm::lessThan(light, glm::vec3(1e-3f))))
                    continue;
                particles::laser_beams(laser, at, fireworks.ways);
                for (const glm::vec3& way : fireworks.ways)
                {
                    const glm::vec3 along = glm::normalize(from.axes * way);
                    const glm::vec3 a = from.position;
                    const glm::vec3 b = a + along * length;
                    // At each end a band across the beam as the eye sees it, bright down its middle, dark at its edges.
                    const auto across = [&](glm::vec3 p, f32 half) {
                        const glm::vec3 side = glm::cross(along, eye - p);
                        const f32 size = glm::length(side);
                        return size > 1e-6f ? side / size * std::max(half, glm::distance(eye, p) * kThinnestSeen) : glm::vec3(0.0f);
                    };
                    const glm::vec3 across_a = across(a, near);
                    const glm::vec3 across_b = across(b, far);
                    const glm::vec4 bright { light, 1 };
                    const glm::vec4 faint { light * kFarEnd, 1 };
                    const glm::vec4 dark { 0, 0, 0, 1 };
                    for (const f32 side : { -1.0f, 1.0f })
                    {
                        const ParkFireworks::BeamCorner edge_a { a + side * across_a, dark };
                        const ParkFireworks::BeamCorner middle_a { a, bright };
                        const ParkFireworks::BeamCorner middle_b { b, faint };
                        const ParkFireworks::BeamCorner edge_b { b + side * across_b, dark };
                        fireworks.beam_corners.insert(fireworks.beam_corners.end(), { edge_a, middle_a, middle_b, edge_a, middle_b, edge_b });
                    }
                }
            }
        }
        if (fireworks.beam_corners.empty())
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(fireworks.beam_corners)));
        if (!vertices)
            return;
        render::DrawCommand draw;
        draw.pipeline = fireworks.beams;
        draw.vertices = *vertices;
        draw.count = static_cast<u32>(fireworks.beam_corners.size());
        draw.transform = view_projection;
        device.draw(draw);
    }

} // namespace rct3::scene::park
