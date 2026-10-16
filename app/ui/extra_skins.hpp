// SPDX-License-Identifier: GPL-3.0-or-later
//
// Skins a screen needs beyond those every screen loads, added to the interface's skins when the screen first
// needs them: the pictures of path styles, for one, live in overlays of their own.
#pragma once

#include "ui/interface_assets.hpp"

#include <span>
#include <string_view>

namespace rct3::ui
{

    /// Adds the skins of each overlay in `files` (paths in the install) to `assets`, with their pictures.
    /// Skins and pictures it already has keep theirs. Files that are missing or unreadable are skipped and
    /// logged. Returns how many skins were added.
    std::size_t add_extra_skins(render::Device& device, InterfaceAssets& assets, std::span<const std::string_view> files);

} // namespace rct3::ui
