// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/design_placement.hpp"

#include "park/object_placement.hpp"

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

    std::vector<PoolOpening> piece_pool_openings(const save::PlacedObject& object, const PieceRecord& record)
    {
        std::vector<PoolOpening> out;
        for (const PieceOpening& o : record.pool_sides)
        {
            const save::Tile tile = square_tile(object, record.squares, o.x, o.z);
            out.push_back({ .col = tile.col, .row = tile.row, .height = object.base_height.value_or(0) + o.height, .side = o.side.turned(object.rotation) });
        }
        return out;
    }

    bool meets_pools(const PoolMap& map, std::span<const PoolOpening> openings)
    {
        return std::ranges::all_of(openings, [&](const PoolOpening& o) {
            const auto [dc, dr] = o.side.step();
            const auto square = map.at(o.col + dc, o.row + dr, o.height);
            return square && square->surface == o.height;
        });
    }

} // namespace rct3::park
