// SPDX-License-Identifier: GPL-3.0-or-later
//
// Skeletons for simple animations: a short list of named bones, each hanging from an earlier one or from
// nothing, and the animations that move them, by name. Each animation is a simple animation item with one
// channel per bone, in the skeleton's bone order (see simple_anim.hpp).
#pragma once

#include "ovl/overlay.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct Skeleton
    {
        std::vector<std::string_view> bones;
        std::vector<std::int32_t> parents; ///< One per bone: the index of an earlier bone, or -1 for none
        std::vector<std::string_view> animation_names;
        std::vector<std::string_view> animations; ///< One "san" item per name
    };

    struct SkeletonItem
    {
        std::string_view name;
        std::optional<Skeleton> skeleton;
    };

    /// Skeletons of one part of an overlay: pointer symbols tagged "sal".
    [[nodiscard]] std::vector<SkeletonItem> read_skeletons(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
