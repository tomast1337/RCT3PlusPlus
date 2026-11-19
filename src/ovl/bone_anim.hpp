// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bone animations: per-bone keyframe tracks for position and rotation over a fixed duration.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One keyframe of a track: a time in seconds and three values.
    struct BoneKeyframe
    {
        f32 time = 0;
        /// Translation for a position track. For a rotation track a rotation vector: its direction is the axis
        /// and its length the angle in radians, so the three values are not separate per-axis angles and have no
        /// axis order. For a single axis it matches the bone's stored rotation under the row-vector convention
        /// [[1,0,0],[0,cos,sin],[0,-sin,cos]]. Checked on thousands of bones whose first keyframe equals the
        /// rest pose. How keyframes are blended between two times is not confirmed.
        std::array<f32, 3> value { };
    };

    struct BoneTrack
    {
        std::string_view name;               ///< Name of the bone the tracks belong to, a string in the file
        std::vector<BoneKeyframe> positions; ///< Empty when the bone does not move
        std::vector<BoneKeyframe> rotations; ///< Empty when the bone does not turn
    };

    struct BoneAnim
    {
        /// Length of the animation in seconds. Equals the last keyframe time of every item seen.
        f32 duration = 0;
        std::vector<BoneTrack> bones;
    };

    struct BoneAnimItem
    {
        std::string_view name;        ///< Symbol name without the tag
        std::optional<BoneAnim> anim; ///< Nothing when any part of the structure is unreadable
    };

    /// Bone animations of one part of an overlay: pointer symbols tagged "ban". Views in the result point
    /// into the overlay's bytes.
    [[nodiscard]] std::vector<BoneAnimItem> read_bone_anims(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
