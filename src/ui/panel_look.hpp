// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a panel's frame is drawn. Panels move from the game's pictures to shapes drawn in code (shape.hpp) one at
// a time: each panel's style says which it uses, so one that does not yet look right keeps its pictures.
// Icons and other pictures inside a frame stay pictures either way.
#pragma once

#include <cstdint>
#include <string_view>

namespace rct3::ui
{

    enum class PanelLook : std::uint8_t
    {
        Texture, ///< Stretched from the game's skins
        Coded,   ///< Drawn as shapes, sharp at any size
    };

    [[nodiscard]] constexpr std::string_view panel_look_name(PanelLook look)
    {
        return look == PanelLook::Coded ? "coded" : "texture";
    }

} // namespace rct3::ui
