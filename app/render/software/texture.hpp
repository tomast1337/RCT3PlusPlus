// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software backend's pictures: textures, targets and the screen, and how shaders read them. A target keeps
// its colour as Rgba8 and its depth as floats, 0 near to 1 far, as the GPU backends' targets do.
#pragma once

#include "core/types.hpp"
#include "render/types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::render::software
{

    struct Texture
    {
        Extent extent;
        TextureFormat format = TextureFormat::Rgba8;
        std::vector<u32> texels;       ///< Rgba8: red in the lowest byte, rows top to bottom
        std::vector<glm::vec4> floats; ///< Rgba32Float
        std::vector<f32> depth;        ///< Targets only
        bool target = false;
        /// A target whose colour a draw or a read has used. Until then passes into it need only its depth (as a
        /// shadow map's), so their fragments are not shaded.
        bool colour_read = false;
        /// Targets made with readable depth: the id their depth is drawn with under.
        u32 depth_id = 0;
        /// An entry that stands for a target's depth: reads that target's depth instead, in red.
        std::optional<const Texture&> depth_of;
    };

    [[nodiscard]] constexpr u32 pack(glm::vec4 c)
    {
        // NaN reads as 0.
        const auto byte = [](f32 v) { return static_cast<u32>((v > 0.0f ? std::min(v, 1.0f) : 0.0f) * 255.0f + 0.5f); };
        return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24);
    }

    [[nodiscard]] constexpr glm::vec4 unpack(u32 c)
    {
        constexpr f32 kUnit = 1.0f / 255.0f;
        return glm::vec4(static_cast<f32>(c & 255u), static_cast<f32>((c >> 8) & 255u), static_cast<f32>((c >> 16) & 255u), static_cast<f32>(c >> 24)) * kUnit;
    }

    /// The texel at (x, y), kept inside the picture, as the shaders' Load reads it.
    [[nodiscard]] glm::vec4 load(const Texture& texture, i32 x, i32 y);

    /// What a sampler with `filter` and `address` reads at `uv` (0..1 across the picture), as the shaders' Sample.
    [[nodiscard]] glm::vec4 sample(const Texture& texture, glm::vec2 uv, Filter filter, AddressMode address);

    /// The red channels of the 2 by 2 texels a linear read at `uv` blends, as the shaders' Gather orders them:
    /// (0, 1), (1, 1), (1, 0), (0, 0) from the top-left one.
    [[nodiscard]] glm::vec4 gather_red(const Texture& texture, glm::vec2 uv, AddressMode address);

    [[nodiscard]] inline glm::vec2 size_of(const Texture& texture)
    {
        return { static_cast<f32>(texture.extent.width), static_cast<f32>(texture.extent.height) };
    }

} // namespace rct3::render::software
