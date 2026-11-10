// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/design_placement.hpp"

#include "park/object/placement.hpp"

#include <algorithm>

namespace rct3::park
{

    std::string_view refusal_key(DesignRefusal refusal) noexcept
    {
        switch (refusal)
        {
            case DesignRefusal::None:
                return { };
            case DesignRefusal::NotOwned:
                return "PlaceObject_NotOwned";
            case DesignRefusal::TooHigh:
                return "PlaceObject_TooHigh";
            case DesignRefusal::Intersects:
                return "PlaceObject_Intersects";
            case DesignRefusal::IntoGround:
                return "PlaceObject_IntersectsGround";
            case DesignRefusal::OffPoolEdge:
                return "PlaceObject_MustEndOnPoolEdge";
        }
        return { };
    }

    bool stands_too_high(float lowest, std::span<const float> grounds) noexcept
    {
        return std::ranges::any_of(grounds, [&](float ground) { return lowest - ground > kDesignRaise; });
    }

    std::vector<pools::PoolOpening> piece_pool_openings(const save::model::PlacedObject& object, const track::PieceRecord& record)
    {
        std::vector<pools::PoolOpening> out;
        for (const track::PieceOpening& o : record.pool_sides)
        {
            const save::model::Tile tile = object::square_tile(object, record.squares, o.x, o.z);
            out.push_back({ .col = tile.col, .row = tile.row, .height = object.base_height.value_or(0) + o.height, .side = o.side.turned(object.rotation) });
        }
        return out;
    }

    bool meets_pools(const pools::PoolMap& map, std::span<const pools::PoolOpening> openings)
    {
        return std::ranges::all_of(openings, [&](const pools::PoolOpening& o) {
            const auto [dc, dr] = o.side.step();
            const auto square = map.at(o.col + dc, o.row + dr, o.height);
            return square && square->surface == o.height;
        });
    }

} // namespace rct3::park
