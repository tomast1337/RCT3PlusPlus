// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_fences.hpp"

#include "park/object_placement.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace rct3::park
{

    namespace
    {

        using save::Tile;

        /// The fence a ride is drawn with, and its colours.
        struct FenceLook
        {
            const std::string* symbol = nullptr;
            const std::string* overlay = nullptr;
            std::array<std::int32_t, 3> colours { };
        };

        /// The tile across edge `turns` of `tile`. An edge is named by the quarter turn that points an edge
        /// piece's outward side at it.
        Tile across(Tile tile, std::int32_t turns)
        {
            const glm::vec2 x = turned_x(turns);
            return { tile.col + static_cast<int>(std::lround(x.x)), tile.row + static_cast<int>(std::lround(x.y)) };
        }

        Tile tile_of(const save::PlacementSpot& spot)
        {
            return { spot.col, spot.row };
        }

        class Builder
        {
        public:
            Builder(const FenceLook& look, std::uint64_t last_id)
                : look_(look)
                , last_id_(last_id)
            {
            }

            void add(Tile tile, std::int32_t turns, std::int32_t height)
            {
                save::PlacedObject& o = pieces_.emplace_back();
                o.id = ++last_id_;
                o.owner_ref = o.id;
                o.fixture = true;
                o.symbol = *look_.symbol;
                o.overlay = *look_.overlay;
                o.col = tile.col;
                o.row = tile.row;
                o.rotation = turns & 3;
                o.colours = look_.colours;
                o.base_height = height;
                o.force_absolute_height = true; // Level at the fenced thing's height, not following the ground
            }

            [[nodiscard]] std::vector<save::PlacedObject> take()
            {
                return std::move(pieces_);
            }

        private:
            const FenceLook& look_;
            std::uint64_t last_id_;
            std::vector<save::PlacedObject> pieces_;
        };

        /// The panels round a flat ride's footprint. Squares count along the model's own X and Z; `gaps` are the
        /// tiles a panel must not shut off.
        void fence_footprint(Builder& out, const save::PlacementSpot& spot, const ObjectModel& item, std::array<std::optional<Tile>, 2> gaps)
        {
            const auto [xs, zs] = item.squares;
            const bool outline = !item.fence_outline.empty();
            const auto inside = [&](int x, int z) {
                if (x < 0 || z < 0 || x >= xs || z >= zs)
                    return false;
                return !outline || item.fence_outline[static_cast<std::size_t>(z) * static_cast<std::size_t>(xs) + static_cast<std::size_t>(x)];
            };

            save::PlacedObject placed;
            placed.col = spot.col;
            placed.row = spot.row;
            placed.rotation = spot.direction;
            for (int z = 0; z < zs; ++z)
            {
                for (int x = 0; x < xs; ++x)
                {
                    if (!inside(x, z))
                        continue;
                    const Tile tile = square_tile(placed, item.squares, x, z);

                    // Each side, as the step to the neighbouring square and the edge's turn from the model's.
                    // A rectangle is fenced on its rim only (both sides of one a single square across); an outline
                    // wherever the neighbour is outside it.
                    struct Side
                    {
                        int dx, dz;
                        std::int32_t turns;
                    };
                    static constexpr std::array<Side, 4> kSides { Side { -1, 0, 0 }, Side { 0, 1, 1 }, Side { 1, 0, 2 }, Side { 0, -1, 3 } };
                    for (const Side& side : kSides)
                    {
                        if (inside(x + side.dx, z + side.dz))
                            continue;
                        const std::int32_t turns = (spot.direction + side.turns) & 3;
                        const Tile beyond = across(tile, turns);
                        if (beyond == gaps[0] || beyond == gaps[1])
                            continue;
                        out.add(tile, turns, spot.height);
                    }
                }
            }
        }

    } // namespace

    std::vector<save::PlacedObject> ride_fences(const save::Ride& ride, const ObjectModel* item, std::uint64_t last_id)
    {
        if (ride.kind == save::RideKind::Special)
            return { };
        FenceLook look;
        if (!ride.override_fence_symbol.empty())
        {
            look.symbol = &ride.override_fence_symbol;
            look.overlay = &ride.override_fence_overlay;
        }
        else if (!ride.fence_symbol.empty())
        {
            look.symbol = &ride.fence_symbol;
            look.overlay = &ride.fence_overlay;
        }
        else
            return { };
        look.colours = { ride.fence_colours[0], ride.fence_colours[1], ride.fence_colours[2] };

        Builder out(look, last_id);
        if (ride.kind == save::RideKind::Flat && ride.base.spot && item && !item->water_only)
        {
            // Only the first station's entrance and exit open a gap in the ring.
            std::array<std::optional<Tile>, 2> gaps;
            if (!ride.stations.empty())
            {
                const save::Station& first = ride.stations.front();
                if (first.entrance_spot)
                    gaps[0] = tile_of(*first.entrance_spot);
                if (first.exit_spot)
                    gaps[1] = tile_of(*first.exit_spot);
            }
            fence_footprint(out, *ride.base.spot, *item, gaps);
        }
        for (const save::Station& station : ride.stations)
        {
            for (const auto& spot : { station.entrance_spot, station.exit_spot })
            {
                if (!spot)
                    continue;
                for (const std::int32_t side : { spot->direction - 1, spot->direction + 1 })
                    out.add(tile_of(*spot), side, spot->height);
            }
        }
        return out.take();
    }

} // namespace rct3::park
