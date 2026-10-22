// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lamps lit round the park, packed for the shaders: the lamps themselves, and a grid over the ground
// whose cells list the lamps that reach into them, so a point looks only at the few lamps near it however
// many the park has. The layout is render::SceneLight::lamps.
#pragma once

#include "math/glm.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rct3::park
{

    /// Width of the lamps' texture, in texels. Also in app/shaders/lit.frag.
    inline constexpr std::uint32_t kLampTexelsAcross = 256;
    /// Most lamps one cell lists. Also in app/shaders/lit.frag.
    inline constexpr std::uint32_t kLampsPerCell = 16;
    /// Most cells along each side of the grid: the cells grow when the lamps spread wider.
    inline constexpr std::uint32_t kLampCellsAcross = 128;

    struct Lamp
    {
        glm::vec3 position { 0.0f }; ///< World space
        float radius = 0;            ///< How far it reaches
        glm::vec3 colour { 0.0f };   ///< Times its strength
    };

    /// The texture's texels, four floats each, `kLampTexelsAcross` to a row, padded to a whole row.
    struct LampTexels
    {
        std::uint32_t rows = 0;
        std::vector<float> values { };
    };

    /// `lamps` packed with a grid of cells `cell` world units across or wider. Nothing when there are none.
    [[nodiscard]] LampTexels lamp_texels(std::span<const Lamp> lamps, float cell);

} // namespace rct3::park
