// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/texture.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::render::software
{

    namespace
    {

        // Where `i` lands in a row or column `size` long: wrapped round, or held at the edge.
        i32 place(i32 i, i32 size, AddressMode address)
        {
            if (address == AddressMode::Repeat)
            {
                i %= size;
                return i < 0 ? i + size : i;
            }
            return std::clamp(i, 0, size - 1);
        }

        glm::vec4 fetch(const Texture& t, i32 x, i32 y)
        {
            const usize i = static_cast<usize>(y) * t.extent.width + static_cast<usize>(x);
            if (t.depth_of)
            {
                const f32 d = t.depth_of->depth[i];
                return { d, d, d, 1.0f };
            }
            if (t.format == TextureFormat::Rgba32Float)
                return t.floats[i];
            return unpack(t.texels[i]);
        }

        Extent extent_of(const Texture& t)
        {
            return t.depth_of ? t.depth_of->extent : t.extent;
        }

    } // namespace

    glm::vec4 load(const Texture& texture, i32 x, i32 y)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        return fetch(texture, std::clamp(x, 0, static_cast<i32>(e.width) - 1), std::clamp(y, 0, static_cast<i32>(e.height) - 1));
    }

    glm::vec4 sample(const Texture& texture, glm::vec2 uv, Filter filter, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const i32 w = static_cast<i32>(e.width);
        const i32 h = static_cast<i32>(e.height);
        // A NaN coordinate reads the corner rather than an undefined place.
        const f32 u = std::isfinite(uv.x) ? uv.x * static_cast<f32>(w) : 0.0f;
        const f32 v = std::isfinite(uv.y) ? uv.y * static_cast<f32>(h) : 0.0f;
        if (filter == Filter::Nearest)
            return fetch(texture, place(static_cast<i32>(std::floor(u)), w, address), place(static_cast<i32>(std::floor(v)), h, address));
        const f32 fx = u - 0.5f;
        const f32 fy = v - 0.5f;
        const f32 x0f = std::floor(fx);
        const f32 y0f = std::floor(fy);
        const f32 ax = fx - x0f;
        const f32 ay = fy - y0f;
        const i32 x0 = place(static_cast<i32>(x0f), w, address);
        const i32 x1 = place(static_cast<i32>(x0f) + 1, w, address);
        const i32 y0 = place(static_cast<i32>(y0f), h, address);
        const i32 y1 = place(static_cast<i32>(y0f) + 1, h, address);
        const glm::vec4 top = glm::mix(fetch(texture, x0, y0), fetch(texture, x1, y0), ax);
        const glm::vec4 bottom = glm::mix(fetch(texture, x0, y1), fetch(texture, x1, y1), ax);
        return glm::mix(top, bottom, ay);
    }

    glm::vec4 gather_red(const Texture& texture, glm::vec2 uv, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const i32 w = static_cast<i32>(e.width);
        const i32 h = static_cast<i32>(e.height);
        const i32 x = static_cast<i32>(std::floor(uv.x * static_cast<f32>(w) - 0.5f));
        const i32 y = static_cast<i32>(std::floor(uv.y * static_cast<f32>(h) - 0.5f));
        const i32 x0 = place(x, w, address);
        const i32 x1 = place(x + 1, w, address);
        const i32 y0 = place(y, h, address);
        const i32 y1 = place(y + 1, h, address);
        return { fetch(texture, x0, y1).r, fetch(texture, x1, y1).r, fetch(texture, x1, y0).r, fetch(texture, x0, y0).r };
    }

} // namespace rct3::render::software
