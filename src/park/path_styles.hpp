// SPDX-License-Identifier: GPL-3.0-or-later
//
// The path and queue styles a player can lay, as the install offers them. Each style has a folder of its own
// under the install's path or queue folder, and in it a small overlay ending in "_Stub" that describes the
// style: its name, the strings key of the name it shows, the picture that stands for it in the interface and
// one scenery item per piece. The pieces carry the price of laying one on the ground; every piece of a style
// costs the same in the files seen, so the first piece's price stands for the style.
//
// A path style names its ground pictures, then each piece four times over (the four names are the same in
// every file seen); a queue style names its pieces once each.
#pragma once

#include "ovl/overlay.hpp"
#include "ovl/piece_set.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    struct PathStyle
    {
        std::string name;            ///< The style's own name, which the park's paths name it by
        std::string name_text;       ///< Key of its name in the strings
        std::string icon;            ///< Skin that stands for it in the interface
        std::int32_t piece_cost = 0; ///< Of one piece laid on the ground, in hundredths of the currency unit
        bool queue = false;
        std::vector<std::string> pieces; ///< Symbols of the pieces raised paths are drawn with, in the order named
        /// The describing overlay, as a park names the overlay of a placed piece: relative to the install,
        /// without its ".common.ovl" ending. Empty when not read from a folder.
        std::string overlay;
        /// The support set its pieces name. What a ride, stall or door paved in it stands on once placed, too.
        std::string support_group { };
    };

    /// The install's folders of path styles and of queue styles, relative to its root.
    inline constexpr std::string_view kPathStyleFolder = "Path";
    inline constexpr std::string_view kQueueStyleFolder = "Queue";
    /// The overlays the styles' pictures come from, relative to the install's root.
    inline constexpr std::string_view kPathIconFile = "Path/PathIcons.common.ovl";
    inline constexpr std::string_view kQueueIconFile = "Queue/QueueIcons.common.ovl";

    [[nodiscard]] PathStyle path_style(const ovl::PathType& type);
    [[nodiscard]] PathStyle queue_style(const ovl::QueueType& type);

    /// The path styles, then the queue styles, that the overlay describes; unreadable ones are left out.
    [[nodiscard]] std::vector<PathStyle> path_styles(const ovl::Overlay& overlay);

    /// Whether `file` is the name of a style's describing overlay: its common file, ending in "_Stub", in
    /// any case.
    [[nodiscard]] bool is_style_overlay(std::string_view file);

    /// The styles described in the folders under `folder` (the install's path or queue folder), by folder
    /// name. Folders without a readable description are skipped.
    [[nodiscard]] std::vector<PathStyle> folder_path_styles(const std::filesystem::path& folder);

} // namespace rct3::park
