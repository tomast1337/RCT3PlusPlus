// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animated rides: the kinds of ride that move by playing animations on a fixed model, such as a
// carousel or a ferris wheel.
#pragma once

#include "ovl/attraction.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct AnimatedRide
    {
        AttractionInfo info;
        /// "spl" items, version 4 and 5 files only. Empty slots are normal. Version 1 files keep their
        /// splines in a part of the item that is not read.
        std::array<std::string_view, 4> splines;
    };

    struct AnimatedRideItem
    {
        std::string_view name;            ///< Symbol name without the tag
        std::optional<AnimatedRide> ride; ///< Nothing when the start of the record does not fit its block
    };

    /// Animated rides of one part of an overlay: pointer symbols tagged "anr". Only the start of each item
    /// is read. The rest holds a variable amount of data (animation sets, effects, texture references) that
    /// is not interpreted yet. Views in the result point into the overlay's bytes.
    [[nodiscard]] std::vector<AnimatedRideItem> read_animated_rides(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
