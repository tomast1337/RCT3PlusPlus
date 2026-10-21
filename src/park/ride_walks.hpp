// SPDX-License-Identifier: GPL-3.0-or-later
//
// How riders walk on to a ride and off it: from the spot at the entrance where they hand over their ticket to
// their seat, and from the seat out past the exit.
//
// A ride's entrance and exit buildings are placed facing the ride: along their own x axis, from the path
// side into the ride, with their origin in the middle of their tile, so the tile's edge on the ride's side is
// 2 units in. Guests walk through to that edge.
//
// Flat rides carry their own walkways in their model files: a loop on the ground round the ride, and short
// paths from points on the loop to the seats, each serving the seats nearest its far end. Riders take the
// loop from where they come in to the path to their seat, the shorter way round, and back out the same way
// to where the loop passes nearest the exit.
//
// Tracked rides board from a station platform beside the track, its height and shape set by the platform's
// type. Riders climb the step up to the platform edge, then walk along the edge to beside their seat, and the
// other way to the exit.
#pragma once

#include "math/glm.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// A flat ride's walkways, as points to walk through: the loop, closed, and each path from by the loop to
    /// the seats.
    struct Walkways
    {
        std::vector<glm::vec3> loop;
        std::vector<std::vector<glm::vec3>> paths;
    };

    /// `ways` moved by `place`.
    [[nodiscard]] Walkways placed_walkways(const Walkways& ways, const glm::mat4& place);

    /// A station platform as its type's file sets it, in units across from the middle of the entrance's tile.
    struct Platform
    {
        float height = 0;       ///< Of the platform's top over the entrance's floor
        float offset = 0;       ///< How far past the entrance tile's edge the platform's edge is
        bool walk_along = true; ///< Riders walk along the edge to beside their seat; else they board at the top of the step
        float step_depth = 0.25f;
        float step_start = 2; ///< Where the step up begins
        float inside = 0;     ///< Where guests stand inside the entrance or exit building
    };

    /// The platform a station platform type's file (`tracks/Platforms/<style>/<type>.common.ovl`) sets, by its
    /// values PlatformHeight, PlatformOffset, WalkAlongPlatform, StepXOffset (the step's depth), StepXBase
    /// (where it begins) and InsideEntranceExitXOffset; Platform's own for those it leaves out.
    [[nodiscard]] std::expected<Platform, std::string> load_platform(const std::filesystem::path& path);

    /// A ride's ways on and off, in the park's space.
    struct RideWays
    {
        glm::vec3 ticket { 0.0f };               ///< Where a guest hands over their ticket
        float ticket_heading = 0;                ///< Facing there, round the vertical, 0 along +Z
        std::vector<std::vector<glm::vec3>> on;  ///< By seat, from the ticket spot to beside the seat
        std::vector<std::vector<glm::vec3>> off; ///< By seat, from beside the seat to outside the exit
    };

    /// Where a guest hands over their ticket at an entrance placed at `entrance`: the entrance model's spot
    /// for them, `spot` in its own space, facing along that spot's z axis.
    void set_ticket_spot(RideWays& ways, const glm::mat4& entrance, const glm::mat4& spot);

    /// The ways on and off a flat ride with `walkways` and seats at `seats`, both in the park's space, entered at
    /// `entrance` and left at `exit` (the buildings' places). Without walkways riders go straight from the
    /// entrance to their seat and from the seat to the exit.
    [[nodiscard]] RideWays flat_ride_ways(const Walkways& walkways, std::span<const glm::vec3> seats, const glm::mat4& entrance, const glm::mat4& exit);

    /// The ways on and off a tracked ride whose train waits with its seats at `seats`, by `platform`, entered at
    /// `entrance` and left at `exit`.
    [[nodiscard]] RideWays track_ride_ways(const Platform& platform, std::span<const glm::vec3> seats, const glm::mat4& entrance, const glm::mat4& exit);

} // namespace rct3::park
