// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lamp_grid.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    glm::vec4 texel(const LampTexels& t, std::uint32_t i)
    {
        return { t.values[i * 4], t.values[i * 4 + 1], t.values[i * 4 + 2], t.values[i * 4 + 3] };
    }

    /// The lamps the cell holding world point (x, z) lists, as the shader reads them.
    std::vector<int> listed(const LampTexels& t, float x, float z)
    {
        const glm::vec4 h0 = texel(t, 0);
        const glm::vec4 h1 = texel(t, 1);
        const int cx = static_cast<int>(std::floor((x - h0.x) / h0.z));
        const int cz = static_cast<int>(std::floor((z - h0.y) / h0.z));
        std::vector<int> out;
        if (cx < 0 || cz < 0 || cx >= static_cast<int>(h0.w) || cz >= static_cast<int>(h1.x))
            return out;
        const auto start = static_cast<std::uint32_t>(h1.z) + static_cast<std::uint32_t>(cz * static_cast<int>(h0.w) + cx) * (kLampsPerCell / 4);
        for (std::uint32_t k = 0; k < kLampsPerCell && t.values[start * 4 + k] >= 0; ++k)
            out.push_back(static_cast<int>(t.values[start * 4 + k]));
        return out;
    }
} // namespace

TEST_CASE("no lamps, no texture")
{
    CHECK(lamp_texels({ }, 4).rows == 0);
}

TEST_CASE("each lamp is listed in the cells it reaches and no others")
{
    const std::array lamps { Lamp { .position = { 0, 5, 0 }, .radius = 3, .colour = { 1, 0.5f, 0 } }, Lamp { .position = { 20, 0, 0 }, .radius = 2, .colour = { 0, 0, 1 } } };
    const LampTexels t = lamp_texels(lamps, 4);
    REQUIRE(t.rows == 1);
    CHECK(t.values.size() == kLampTexelsAcross * 4);
    CHECK(texel(t, 1).y == 2);
    CHECK(texel(t, 2) == glm::vec4(0, 5, 0, 3));
    CHECK(texel(t, 3) == glm::vec4(1, 0.5f, 0, 0));
    CHECK(texel(t, 4) == glm::vec4(20, 0, 0, 2));
    CHECK(listed(t, 0, 0) == std::vector { 0 });
    CHECK(listed(t, 20, 1) == std::vector { 1 });
    CHECK(listed(t, 10, 0).empty());
    CHECK(listed(t, 50, 0).empty());
}

TEST_CASE("overlapping lamps share cells, up to the most a cell lists")
{
    std::vector<Lamp> lamps(kLampsPerCell + 3, Lamp { .position = { 1, 0, 1 }, .radius = 1, .colour = { 1, 1, 1 } });
    const LampTexels t = lamp_texels(lamps, 8);
    const std::vector<int> here = listed(t, 1, 1);
    CHECK(here.size() == kLampsPerCell);
    CHECK(here.front() == 0);
}

TEST_CASE("lamps spread far apart widen the cells instead of growing the grid")
{
    const std::array lamps { Lamp { .position = { 0, 0, 0 }, .radius = 1 }, Lamp { .position = { 10000, 0, 0 }, .radius = 1 } };
    const LampTexels t = lamp_texels(lamps, 4);
    CHECK(texel(t, 0).w <= kLampCellsAcross);
    CHECK(texel(t, 0).z >= 10000.0f / kLampCellsAcross);
    CHECK(listed(t, 10000, 0) == std::vector { 1 });
}
