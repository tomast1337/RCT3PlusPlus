// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's firework displays (particles/display.hpp), put on as scheduled: on each day of a month a display
// runs in, once the time of day reaches its start, a message tells the player and the display plays to its
// music. Each shot sets its firework effect off from its tube of the launcher placed in the park; the effect
// itself carries the shell up and bursts it. A laser shot shines its beams from its tube for the laser's run, drawn
// after the world as thin lines of light added to what is behind them. A launcher taken away leaves its shots out.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "park/camera.hpp"
#include "particles/display.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::save
{
    struct SaveFile;
} // namespace rct3::save

namespace rct3::scene::park
{

    struct ParkScene;

    /// Text key of the message a display starting posts; its icon takes the camera to the show.
    inline constexpr std::string_view kDisplayMessage = "FireworkDisplay_AboutToStart";

    struct ParkFireworks
    {
        struct Playing
        {
            usize display = 0; ///< In `displays`
            particles::DisplayRun run { };
            glm::vec3 centre { 0.0f }; ///< Among its launchers, on the ground
            u32 ticks_left = 0;        ///< Of the park's, before it ends and guests stop watching it
        };

        /// A laser shot of the display playing, shining.
        struct Shining
        {
            usize shot = 0; ///< In its display's shots, which carries the laser
            save::model::PlacedObjectIndex object { };
            usize tube = 0;
            f32 shot_at = 0; ///< park_clock when it was shot
        };

        struct BeamCorner
        {
            glm::vec3 position { 0.0f };
            glm::vec4 colour { 0.0f };
        };

        std::vector<particles::Display> displays;
        std::vector<u8> was_due; ///< By display: due at the last look, so a day starts it once
        std::optional<Playing> playing;
        std::vector<Shining> lasers;
        std::vector<glm::vec3> ways;          ///< A laser's beams, filled again for each
        std::vector<BeamCorner> beam_corners; ///< The beams' triangles, built again each frame
        render::PipelineHandle beams;
    };

    /// The displays the park `file` schedules; none when it holds none or they cannot be read.
    void load_park_fireworks(render::Device& device, ParkFireworks& fireworks, const save::SaveFile& file);
    void destroy_park_fireworks(render::Device& device, ParkFireworks& fireworks);
    /// Starts display `display` at once, in place of any playing; false when there is no such display.
    bool start_park_display(ParkScene& scene, usize display);
    void stop_park_display(ParkScene& scene);
    /// The view of the display playing from `yaw`: its launchers and the sky its shells burst in; none when no
    /// display plays or none of its launchers stands.
    std::optional<rct3::park::ParkCamera> park_display_view(const ParkScene& scene, f32 yaw);
    /// A tick of the park's: starts the displays that fall due, and ends the one playing once its time is up, so
    /// guests watch it for the same ticks however the frames fall.
    void tick_park_fireworks(ParkScene& scene);
    /// Moves the display playing on by `park_time`, firing its shots.
    void update_park_fireworks(ParkScene& scene, units::Seconds park_time);
    /// Draws the beams of the lasers shining into the open pass, seen from `eye`.
    void draw_park_lasers(render::Device& device, ParkScene& scene, const glm::mat4& view_projection, glm::vec3 eye);

} // namespace rct3::scene::park
