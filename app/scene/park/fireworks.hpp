// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's firework displays (particles/display.hpp), put on as scheduled: on each day of a month a display
// runs in, once the time of day reaches its start, a message tells the player and the display plays to its
// music. Each shot sets its firework effect off from its tube of the launcher placed in the park; the effect
// itself carries the shell up and bursts it. A launcher taken away leaves its shots out.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "park/camera.hpp"
#include "particles/display.hpp"
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

        std::vector<particles::Display> displays;
        std::vector<u8> was_due; ///< By display: due at the last look, so a day starts it once
        std::optional<Playing> playing;
    };

    /// The displays the park `file` schedules; none when it holds none or they cannot be read.
    void load_park_fireworks(ParkFireworks& fireworks, const save::SaveFile& file);
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

} // namespace rct3::scene::park
