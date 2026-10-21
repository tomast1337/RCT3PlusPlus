// SPDX-License-Identifier: GPL-3.0-or-later
//
// Models moved by bones, such as flat rides: the bones as the model stands, the motions that turn and move
// them, and where each bone is at some time into a motion.
#pragma once

#include "math/glm.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace rct3::park
{

    /// Keys of one bone in one motion: times in seconds, and a value at each.
    struct BoneKeys
    {
        std::vector<float> times;
        std::vector<glm::vec3> values;
    };

    /// How one motion moves each bone of a rig; a bone with no keys keeps where it stands.
    struct BoneMotion
    {
        float seconds = 0;
        std::vector<BoneKeys> positions; ///< By bone: offsets from the parent bone
        std::vector<BoneKeys> rotations; ///< By bone: turns from the parent bone, axis times angle in radians
    };

    struct BoneRig
    {
        std::vector<std::int32_t> parents; ///< By bone, -1 for a root
        std::vector<glm::mat4> rest_local; ///< By bone, standing, from its parent's space to its own
        std::vector<glm::mat4> rest_world; ///< By bone, standing, model space
        std::vector<BoneMotion> motions;   ///< In the order the model lists them
    };

    /// The turn a rotation key stands for: about its direction, by its length in radians.
    [[nodiscard]] glm::mat4 key_turn(const glm::vec3& axis_angle);

    /// By bone of `rig`: how `time` seconds into motion `motion` moves the bone and everything it carries,
    /// from where it stands to where it is (model space). Times outside the motion hold its ends; a motion the
    /// rig lacks leaves every bone standing.
    [[nodiscard]] std::vector<glm::mat4> bone_moves(const BoneRig& rig, std::size_t motion, float time);

    /// Flat rides' motions: standing, starting, running and stopping.
    inline constexpr std::size_t kRideStanding = 0;
    inline constexpr std::size_t kRideStarting = 1;
    inline constexpr std::size_t kRideRunning = 2;
    inline constexpr std::size_t kRideStopping = 3;

    /// Which of a flat ride's motions (with lengths `seconds`) plays, and how far into it: `into` seconds into
    /// a ride of `cycle` seconds (start, running round as many times as fit, stop), or standing `idle` seconds
    /// when not riding.
    [[nodiscard]] std::pair<std::size_t, float> flat_ride_motion(std::span<const float> seconds, float cycle, float into, bool riding, float idle);

    /// A ride entrance's or exit's gate motions: shut, opening, open and shutting.
    inline constexpr std::size_t kGateShut = 0;
    inline constexpr std::size_t kGateOpening = 1;
    inline constexpr std::size_t kGateOpen = 2;
    inline constexpr std::size_t kGateShutting = 3;

    /// The gate motion to play once `playing` has played through, as the gate should be `open` or not. A gate
    /// changes only between motions, so it never jumps; shut and open play again while it stays so.
    [[nodiscard]] std::size_t next_gate_motion(std::size_t playing, bool open);

} // namespace rct3::park
