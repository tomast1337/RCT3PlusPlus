// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_placement.hpp"

#include <array>

namespace rct3::park
{

    glm::vec2 turned_x(std::int32_t turns)
    {
        // Unturned facing -X, then a quarter clockwise each: -X, +Z, +X, -Z.
        static constexpr std::array<glm::vec2, 4> kDirections { glm::vec2 { -1, 0 }, glm::vec2 { 0, 1 }, glm::vec2 { 1, 0 }, glm::vec2 { 0, -1 } };
        return kDirections[static_cast<std::size_t>(((turns % 4) + 4) % 4)];
    }

    glm::vec2 footprint_middle(const save::PlacedObject& object, std::array<std::int32_t, 2> squares)
    {
        // A quarter turn either way lays the model's X along the park's Z.
        const bool across = (object.rotation & 1) != 0;
        const auto span_x = static_cast<float>(squares[across ? 1 : 0]);
        const auto span_z = static_cast<float>(squares[across ? 0 : 1]);
        return { static_cast<float>(object.col) + span_x / 2, static_cast<float>(object.row) + span_z / 2 };
    }

    glm::mat4 object_transform(const save::PlacedObject& object, Positioning positioning, const save::TerrainGrid& grid, std::array<std::int32_t, 2> squares)
    {
        const glm::vec2 x = turned_x(object.rotation);
        const glm::vec2 z { -x.y, x.x }; // The model's +Z, a quarter turn on from its +X

        const glm::vec2 middle = footprint_middle(object, squares);
        glm::vec2 anchor { grid.world_x(0) + middle.x * grid.tile_size_x, grid.world_z(0) + middle.y * grid.tile_size_z };
        switch (positioning)
        {
            case Positioning::Edge:
                anchor += x * kEdgeOffset;
                break;
            case Positioning::PathSide:
            case Positioning::PathSideOuter:
            case Positioning::PathSideJoin:
                anchor += x * kPathSideOffset;
                break;
            case Positioning::Quarter:
            {
                const int q = object.quadrant & 3;
                anchor += glm::vec2 { q % 2 == 0 ? -kQuarterOffset : kQuarterOffset, q / 2 == 0 ? -kQuarterOffset : kQuarterOffset };
                break;
            }
            default:
                break;
        }

        float y = static_cast<float>(object.base_height.value_or(0));
        const bool on_ground = !object.base_height || (object.height_offset == 0 && !object.force_absolute_height);
        if (on_ground)
        {
            if (const auto ground = grid.height_at(anchor.x, anchor.y))
                y = *ground;
        }
        y += object.height_adjust.value_or(0);

        glm::mat4 m { 1.0f };
        m[0] = glm::vec4(x.x, 0, x.y, 0);
        m[1] = glm::vec4(0, 1, 0, 0);
        m[2] = glm::vec4(z.x, 0, z.y, 0);
        m[3] = glm::vec4(anchor.x, y, anchor.y, 1);
        return m;
    }

} // namespace rct3::park
