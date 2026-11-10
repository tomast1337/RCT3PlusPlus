// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride walks: riders on their feet, walking from a ride's ticket spot to their seat and from their seat out past
// the exit. The components here, and the step that moves them.
#pragma once

#include "math/glm.hpp"
#include "profiling/profiler.hpp"
#include "simulation/units.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;
    enum class GestureKind : std::uint8_t;

    /// The ways riders walk on and off a ride, in the park's space, worked out from the ride's models where it
    /// is drawn. Seats are counted as a load fills them: a ride's riders, or a train's. A ride without one lets
    /// its riders on and off where they stand.
    struct RideWalks
    {
        glm::vec3 ticket { 0.0f };  ///< Where a guest stands at the entrance to hand over their ticket
        Radians ticket_heading { }; ///< The way they face there, as Pose::heading
        /// By seat: from the ticket spot to the seat, and from the seat out past the exit.
        std::vector<std::vector<glm::vec3>> on { };
        std::vector<std::vector<glm::vec3>> off { };
    };

    /// A rider walking on to their seat from the ticket spot, or off it to the paths: still riding, but on
    /// their feet. `points` are walked in turn; the first, on the way on, is the ticket spot, where they stand
    /// for `pause` seconds first.
    struct RideWalk
    {
        std::vector<glm::vec3> points;
        std::size_t next = 0; ///< The point walked to now
        Seconds pause { };
        bool off = false;
        std::optional<GestureKind> gesture { }; ///< Shown once off, on the paths
    };

    /// One tick of the riders on their feet: they walk on along their points, standing a moment at the ticket
    /// spot, and once off a ride go on from its exit. Times the phase on `phase`.
    void step_ride_walks(SimWorld& world, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
