// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::render::software
{

    namespace
    {

        // Where `i` lands in a row or column `size` long: wrapped round, or held at the edge.
        std::int32_t place(std::int32_t i, std::int32_t size, AddressMode address)
        {
            if (address == AddressMode::Repeat)
            {
                i %= size;
                return i < 0 ? i + size : i;
            }
            return std::clamp(i, 0, size - 1);
        }

        glm::vec4 fetch(const Texture& t, std::int32_t x, std::int32_t y)
        {
            const std::size_t i = static_cast<std::size_t>(y) * t.extent.width + static_cast<std::size_t>(x);
            if (t.depth_of)
            {
                const float d = t.depth_of->depth[i];
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

    glm::vec4 load(const Texture& texture, std::int32_t x, std::int32_t y)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        return fetch(texture, std::clamp(x, 0, static_cast<std::int32_t>(e.width) - 1), std::clamp(y, 0, static_cast<std::int32_t>(e.height) - 1));
    }

    glm::vec4 sample(const Texture& texture, glm::vec2 uv, Filter filter, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const std::int32_t w = static_cast<std::int32_t>(e.width);
        const std::int32_t h = static_cast<std::int32_t>(e.height);
        // A NaN coordinate reads the corner rather than an undefined place.
        const float u = std::isfinite(uv.x) ? uv.x * static_cast<float>(w) : 0.0f;
        const float v = std::isfinite(uv.y) ? uv.y * static_cast<float>(h) : 0.0f;
        if (filter == Filter::Nearest)
            return fetch(texture, place(static_cast<std::int32_t>(std::floor(u)), w, address), place(static_cast<std::int32_t>(std::floor(v)), h, address));
        const float fx = u - 0.5f;
        const float fy = v - 0.5f;
        const float x0f = std::floor(fx);
        const float y0f = std::floor(fy);
        const float ax = fx - x0f;
        const float ay = fy - y0f;
        const std::int32_t x0 = place(static_cast<std::int32_t>(x0f), w, address);
        const std::int32_t x1 = place(static_cast<std::int32_t>(x0f) + 1, w, address);
        const std::int32_t y0 = place(static_cast<std::int32_t>(y0f), h, address);
        const std::int32_t y1 = place(static_cast<std::int32_t>(y0f) + 1, h, address);
        const glm::vec4 top = glm::mix(fetch(texture, x0, y0), fetch(texture, x1, y0), ax);
        const glm::vec4 bottom = glm::mix(fetch(texture, x0, y1), fetch(texture, x1, y1), ax);
        return glm::mix(top, bottom, ay);
    }

    glm::vec4 gather_red(const Texture& texture, glm::vec2 uv, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const std::int32_t w = static_cast<std::int32_t>(e.width);
        const std::int32_t h = static_cast<std::int32_t>(e.height);
        const std::int32_t x = static_cast<std::int32_t>(std::floor(uv.x * static_cast<float>(w) - 0.5f));
        const std::int32_t y = static_cast<std::int32_t>(std::floor(uv.y * static_cast<float>(h) - 0.5f));
        const std::int32_t x0 = place(x, w, address);
        const std::int32_t x1 = place(x + 1, w, address);
        const std::int32_t y0 = place(y, h, address);
        const std::int32_t y1 = place(y + 1, h, address);
        return { fetch(texture, x0, y1).r, fetch(texture, x1, y1).r, fetch(texture, x1, y0).r, fetch(texture, x0, y0).r };
    }

} // namespace rct3::render::software
