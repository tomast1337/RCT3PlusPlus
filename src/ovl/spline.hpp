// SPDX-License-Identifier: GPL-3.0-or-later
//
// Splines: smooth curves through a list of 3D points, used for ride tracks, paths and lift cars.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One control point of a spline.
    struct SplineNode
    {
        std::array<float, 3> position { };
        /// Two direction vectors per node. The first is zero at the start of an open spline and the second
        /// zero at its end, so they are probably the incoming and outgoing tangents of a cubic segment.
        /// The scale convention is not confirmed.
        std::array<float, 3> tangent_in { };
        std::array<float, 3> tangent_out { };
    };

    struct Spline
    {
        /// Set in a few hundred splines. One more segment length is stored when it is set, so it most likely
        /// marks a loop whose last node joins the first. Not confirmed.
        bool closed = false;
        float length = 0;     ///< Total length, equal to the sum of the segment lengths
        float max_height = 0; ///< Largest Y of the nodes in nearly every item
        std::vector<SplineNode> nodes;
        std::vector<float> segment_lengths; ///< Length of each segment between consecutive nodes
    };

    struct SplineItem
    {
        std::string_view name;        ///< Symbol name without the tag
        std::optional<Spline> spline; ///< Nothing when any part of the structure is unreadable
    };

    /// Splines of one part of an overlay: pointer symbols tagged "spl". A 28-byte block follows the segment
    /// lengths in the file. Its purpose is not known and it is not read.
    [[nodiscard]] std::vector<SplineItem> read_splines(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
