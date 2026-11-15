// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/texture.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::render::software
{

    namespace
    {

        // Where `i` lands in a row or column `size` long: wrapped round, or held at the edge.
        int place(int i, int size, AddressMode address)
        {
            if (address == AddressMode::Repeat)
            {
                i %= size;
                return i < 0 ? i + size : i;
            }
            return std::clamp(i, 0, size - 1);
        }

        glm::vec4 fetch(const Texture& t, int x, int y)
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

    glm::vec4 load(const Texture& texture, int x, int y)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        return fetch(texture, std::clamp(x, 0, static_cast<int>(e.width) - 1), std::clamp(y, 0, static_cast<int>(e.height) - 1));
    }

    glm::vec4 sample(const Texture& texture, glm::vec2 uv, Filter filter, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const int w = static_cast<int>(e.width);
        const int h = static_cast<int>(e.height);
        // A NaN coordinate reads the corner rather than an undefined place.
        const float u = std::isfinite(uv.x) ? uv.x * static_cast<float>(w) : 0.0f;
        const float v = std::isfinite(uv.y) ? uv.y * static_cast<float>(h) : 0.0f;
        if (filter == Filter::Nearest)
            return fetch(texture, place(static_cast<int>(std::floor(u)), w, address), place(static_cast<int>(std::floor(v)), h, address));
        const float fx = u - 0.5f;
        const float fy = v - 0.5f;
        const float x0f = std::floor(fx);
        const float y0f = std::floor(fy);
        const float ax = fx - x0f;
        const float ay = fy - y0f;
        const int x0 = place(static_cast<int>(x0f), w, address);
        const int x1 = place(static_cast<int>(x0f) + 1, w, address);
        const int y0 = place(static_cast<int>(y0f), h, address);
        const int y1 = place(static_cast<int>(y0f) + 1, h, address);
        const glm::vec4 top = glm::mix(fetch(texture, x0, y0), fetch(texture, x1, y0), ax);
        const glm::vec4 bottom = glm::mix(fetch(texture, x0, y1), fetch(texture, x1, y1), ax);
        return glm::mix(top, bottom, ay);
    }

    glm::vec4 gather_red(const Texture& texture, glm::vec2 uv, AddressMode address)
    {
        const Extent e = extent_of(texture);
        if (e.width == 0 || e.height == 0)
            return glm::vec4(0.0f);
        const int w = static_cast<int>(e.width);
        const int h = static_cast<int>(e.height);
        const int x = static_cast<int>(std::floor(uv.x * static_cast<float>(w) - 0.5f));
        const int y = static_cast<int>(std::floor(uv.y * static_cast<float>(h) - 0.5f));
        const int x0 = place(x, w, address);
        const int x1 = place(x + 1, w, address);
        const int y0 = place(y, h, address);
        const int y1 = place(y + 1, h, address);
        return { fetch(texture, x0, y1).r, fetch(texture, x1, y1).r, fetch(texture, x1, y0).r, fetch(texture, x0, y0).r };
    }

} // namespace rct3::render::software
