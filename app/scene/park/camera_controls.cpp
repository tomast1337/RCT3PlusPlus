// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/camera_controls.hpp"

#include "scene/park/scene.hpp"
#include "scene/pool_tools.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::scene::park
{

    namespace
    {

        // How far the controls move the camera.
        constexpr float kDragPan = 0.0025f;            ///< Per pixel, times the distance
        constexpr float kDragTurn = 0.006f;            ///< Radians per pixel
        constexpr float kKeyPan = 0.8f;                ///< Per second, times the distance
        constexpr float kEdgeBand = 12;                ///< Interface units in from a screen edge where the pointer pushes
        constexpr float kWheelZoom = 0.85f;            ///< Distance times this per notch towards the screen
        constexpr units::Seconds kWheelRoll { 0.25f }; ///< How long a notch takes to roll in, nearly all of it
        constexpr float kKeyZoom = 2.5f;               ///< Times per second
        constexpr units::TurnRate kKeyTurn { 1.5f };
        constexpr float kIsoTurnDrag = 60;                ///< Interface units across, times the sensitivity option, that turn isometric a quarter
        constexpr units::TurnRate kIsoTurnSpeed { 3.5f }; ///< Isometric turns at
        constexpr float kDragZoom = 1.006f;               ///< Distance times this per pixel down, advanced's right drag
        constexpr float kWheelTilt = 0.08f;               ///< Radians down per notch away, advanced's wheel in its first scheme
        constexpr units::Pace kFlySpeed { 80 };           ///< Free look flies at, times its speed option
        constexpr float kFlyDrag = 0.004f;                ///< Seconds of flight per pixel a middle drag goes
        constexpr float kFlyAboveGround = 1.5f;           ///< Free look's eye stays this high over the ground

    } // namespace

    void move_camera(ParkScene& scene, const ParkInput& input, bool over_interface, const config::game::GameOptions& options, units::Seconds elapsed)
    {
        const float dt = elapsed.value;
        using rct3::park::ControlMode;
        const ui::Point p = input.pointer.position;
        const float dx = p.x - scene.last_pointer.x;
        const float dy = p.y - scene.last_pointer.y;
        scene.last_pointer = p;
        const auto mode = static_cast<ControlMode>(options.choice(config::game::GameOption::CameraMode));
        const float scale = scene.scale.value_or(1);

        rct3::park::ParkCamera c = scene.camera;
        if (mode == ControlMode::Isometric && scene.control_mode != mode)
        {
            scene.iso_step = rct3::park::isometric_step(c.yaw);
            c.pitch = rct3::park::kIsometricPitch;
        }
        scene.control_mode = mode;

        // A drag starts away from the windows and goes on wherever the pointer then goes. Which buttons hold
        // it decides what it does: the right one, the right with the left, or the middle one.
        const DragButtons started = scene.drag_buttons;
        const DragButtons buttons = input.middle ? DragButtons::Middle : input.right && input.pointer.down ? DragButtons::Both : input.right ? DragButtons::Right : DragButtons::None;
        scene.drag_buttons = scene.drag_buttons != DragButtons::None || !over_interface ? buttons : DragButtons::None;
        const DragButtons held = scene.drag_buttons;
        if (mode == ControlMode::Advanced && held != DragButtons::None && started == DragButtons::None)
        {
            // A drag turns round the point under the pointer when it starts (or the screen's middle): the
            // ground, or unless the option says not, the scenery in front of it.
            const ui::Rect screen = scene.desktop->root().rect();
            const glm::vec2 size = glm::vec2 { screen.width(), screen.height() } * scale;
            const glm::vec2 at = options.flag(config::game::GameOption::CameraFocusOnMiddle) ? size / 2.0f : glm::vec2 { p.x, p.y };
            const rct3::park::Ray ray = rct3::park::view_ray(c, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            std::optional<float> hit = rct3::park::object::ground_hit(ray, scene.grid);
            if (!options.flag(config::game::GameOption::CameraIgnoresScenery) && scene.show_scenery)
            {
                if (scene.pick_boxes_of != scene.objects.size())
                    place_objects(scene);
                if (const auto object = rct3::park::object::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) { return !scene.objects[i].hidden; }))
                    hit = object->distance;
            }
            scene.pivot = hit ? std::optional(ray.origin + ray.direction * *hit) : std::nullopt;
        }
        if (held == DragButtons::None)
            scene.pivot.reset();
        const bool free = scene.free_camera;
        scene.turning = false;
        scene.dragging = false;
        const auto turn = [&](float yaw, float pitch) {
            c = scene.pivot && !free ? rct3::park::orbited(c, *scene.pivot, yaw, pitch) : rct3::park::turned(c, yaw, pitch, free);
            scene.turning = true;
        };
        const auto pan = [&](float right, float forward) {
            c = rct3::park::panned(c, right, forward);
            scene.dragging = true;
        };
        // Notches to zoom; free look has no zoom.
        float wheel = over_interface || mode == ControlMode::Freelook ? 0 : input.pointer.wheel.y;
        const units::Pace fly { options.get(config::game::GameOption::FreeLookSpeed) * kFlySpeed.value };

        switch (mode)
        {
            case ControlMode::Isometric:
                if (held == DragButtons::Right)
                    pan(-dx * kDragPan * c.distance, dy * kDragPan * c.distance);
                else if (held == DragButtons::Both)
                {
                    // Far enough across, the view turns a quarter; then the drag counts afresh.
                    scene.iso_drag += dx;
                    const float needed = options.get(config::game::GameOption::IsometricRotation) * kIsoTurnDrag * scale;
                    if (std::abs(scene.iso_drag) >= needed)
                    {
                        scene.iso_step += scene.iso_drag > 0 ? 1 : -1;
                        scene.iso_drag = 0;
                    }
                    scene.turning = true;
                }
                if (held != DragButtons::Both)
                    scene.iso_drag = 0;
                break;
            case ControlMode::Freelook:
                // The right button looks round, the middle one flies across and ahead.
                if (held == DragButtons::Right)
                {
                    c = rct3::park::turned_at_eye(c, dx * kDragTurn, dy * kDragTurn);
                    scene.turning = true;
                }
                else if (held == DragButtons::Middle)
                {
                    c = rct3::park::flown(c, glm::vec3 { dx, 0, -dy } * kFlyDrag * fly.value);
                    scene.dragging = true;
                }
                break;
            case ControlMode::Advanced:
            {
                // Three ways to share the buttons out, as the camera controls option picks; the middle
                // button always turns and tilts.
                const int scheme = options.choice(config::game::GameOption::CameraControls);
                const bool turns = held == DragButtons::Middle || (scheme == 1 && held == DragButtons::Both) || (scheme == 2 && held == DragButtons::Right);
                const bool slides = (scheme == 1 && held == DragButtons::Right) || (scheme != 1 && held == DragButtons::Both);
                if (turns)
                    turn(dx * kDragTurn, dy * kDragTurn);
                else if (slides)
                    pan(-dx * kDragPan * c.distance, dy * kDragPan * c.distance);
                else if (scheme == 0 && held == DragButtons::Right)
                {
                    // Across turns, up and down brings the view nearer and farther.
                    turn(dx * kDragTurn, 0);
                    c = rct3::park::zoomed(c, std::pow(kDragZoom, dy), free);
                }
                if (scheme == 0)
                {
                    // The wheel tilts instead of zooming.
                    if (wheel != 0)
                        c = rct3::park::turned(c, 0, wheel * kWheelTilt, free);
                    wheel = 0;
                }
                break;
            }
            default:
                if (held == DragButtons::Middle || held == DragButtons::Both)
                    turn(dx * kDragTurn, dy * kDragTurn);
                else if (held == DragButtons::Right)
                    pan(-dx * kDragPan * c.distance, dy * kDragPan * c.distance);
                break;
        }

        scene.zoom_pending += wheel;
        if (scene.zoom_pending != 0)
        {
            const float notches = rct3::park::zoom_step(scene.zoom_pending, dt, kWheelRoll.value);
            scene.zoom_pending -= notches;
            const rct3::park::ParkCamera before = c;
            c = rct3::park::zoomed(c, std::pow(kWheelZoom, notches), free);
            if (c.distance == before.distance)
                scene.zoom_pending = 0; // At a limit: what is left would roll in late once turned back
        }

        glm::vec2 slide { (input.right_key ? 1.0f : 0.0f) - (input.left_key ? 1.0f : 0.0f), (input.up_key ? 1.0f : 0.0f) - (input.down_key ? 1.0f : 0.0f) };
        if (options.flag(config::game::GameOption::AutoScrollAtEdge) && input.in_window && !over_interface && held == DragButtons::None)
        {
            const ui::Rect screen = scene.desktop->root().rect();
            slide += rct3::park::edge_push({ p.x, p.y }, glm::vec2 { screen.width(), screen.height() } * scale, kEdgeBand * scale);
        }
        const float yaw = (input.turn_right_key ? 1.0f : 0.0f) - (input.turn_left_key ? 1.0f : 0.0f);
        const float pitch = (input.tilt_down_key ? 1.0f : 0.0f) - (input.tilt_up_key ? 1.0f : 0.0f);
        if (mode == ControlMode::Freelook)
        {
            // Free look flies the eye: the zoom keys take it up and down.
            const float rise = (input.zoom_out_key ? 1.0f : 0.0f) - (input.zoom_in_key ? 1.0f : 0.0f);
            if (slide != glm::vec2(0) || rise != 0)
                c = rct3::park::flown(c, glm::vec3 { slide.x, rise, slide.y } * fly.value * dt);
            if (yaw != 0 || pitch != 0)
                c = rct3::park::turned_at_eye(c, yaw * kKeyTurn.value * dt, pitch * kKeyTurn.value * dt);
            // The eye stays over the map and above its ground.
            const glm::vec3 at = rct3::park::eye(c);
            glm::vec3 kept = at;
            if (!scene.grid.empty())
            {
                kept.x = std::clamp(kept.x, scene.grid.origin_x, scene.grid.world_x(scene.grid.width) - rct3::park::kNear);
                kept.z = std::clamp(kept.z, scene.grid.origin_z, scene.grid.world_z(scene.grid.height) - rct3::park::kNear);
                if (const auto h = scene.grid.height_at(kept.x, kept.z))
                    kept.y = std::max(kept.y, *h + kFlyAboveGround);
            }
            c.target += kept - at;
            scene.camera = c;
            return;
        }
        if (slide != glm::vec2(0))
            c = rct3::park::panned(c, slide.x * kKeyPan * c.distance * dt, slide.y * kKeyPan * c.distance * dt);
        if (input.zoom_in_key != input.zoom_out_key)
            c = rct3::park::zoomed(c, std::pow(kKeyZoom, (input.zoom_in_key ? -1.0f : 1.0f) * dt), free);
        if (mode == ControlMode::Isometric)
        {
            // The turn keys and the game's isometric turn commands step a quarter, once the last turn is done.
            const float goal = rct3::park::isometric_yaw(scene.iso_step);
            const bool settled = std::abs(std::remainder(c.yaw - goal, glm::two_pi<float>())) < 1e-3f;
            int keyed = settled ? static_cast<int>(yaw) : 0;
            for (const ui::PlayCommand command : input.commands)
                if (command == ui::PlayCommand::RotateIsometric || command == ui::PlayCommand::ReverseIsometric)
                    keyed = command == ui::PlayCommand::ReverseIsometric ? -1 : 1;
            scene.iso_step += keyed;
            c.yaw = rct3::park::yaw_towards(c.yaw, rct3::park::isometric_yaw(scene.iso_step), kIsoTurnSpeed.value * dt);
            c.pitch = rct3::park::kIsometricPitch;
        }
        else if (yaw != 0 || pitch != 0)
            c = rct3::park::turned(c, yaw * kKeyTurn.value * dt, pitch * kKeyTurn.value * dt, free);
        // Advanced turns round points above the ground too. The pool tool's work shown is not yet the
        // ground: the view settles as if it were not there, so a drag's squares stay under the pointer.
        swap_pool_work(scene);
        scene.camera = free ? c : rct3::park::on_ground(c, scene.grid, mode == ControlMode::Advanced);
        // Over a pool it settles on the surface, not on the ground dug out under it, so the view keeps still
        // while the pool tool shows its work there.
        const save::model::TerrainGrid& g = scene.grid;
        const auto pool = free || g.empty() ? std::nullopt : scene.pools.at(static_cast<int>(std::floor((scene.camera.target.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((scene.camera.target.z - g.origin_z) / g.tile_size_z)));
        if (pool)
            scene.camera.target.y = std::max(scene.camera.target.y, static_cast<float>(pool->surface));
        swap_pool_work(scene);
    }

} // namespace rct3::scene::park
