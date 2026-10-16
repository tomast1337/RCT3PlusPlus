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
        /// Two offsets per node, the first zero at the start of an open spline and the second zero at its
        /// end. Each segment is a cubic Bezier curve: from a node, through the node plus its `tangent_out`
        /// and the next node plus its `tangent_in`, to the next node. The stored segment lengths are those
        /// curves' lengths.
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

    /// The point a fraction `t` (0 to 1) of the way along segment `segment` of `spline`, by the curve's
    /// parameter rather than its length. Segment must be below `nodes.size() - 1`.
    [[nodiscard]] std::array<float, 3> spline_point(const Spline& spline, std::size_t segment, float t);

    struct SplineItem
    {
        std::string_view name;        ///< Symbol name without the tag
        std::optional<Spline> spline; ///< Nothing when any part of the structure is unreadable
    };

    /// Splines of one part of an overlay: pointer symbols tagged "spl". A 28-byte block follows the segment
    /// lengths in the file. Its purpose is not known and it is not read.
    [[nodiscard]] std::vector<SplineItem> read_splines(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
