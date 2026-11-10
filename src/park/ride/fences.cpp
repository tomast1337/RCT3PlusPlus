// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride/fences.hpp"

#include "park/object_placement.hpp"

#include <array>
#include <optional>
#include <string>

namespace rct3::park::ride
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

            /// A panel on the edge of `tile` that `turn` points its outward side at.
            void add(Tile tile, ModelTurn turn, std::int32_t height)
            {
                save::PlacedObject& o = pieces_.emplace_back();
                o.id = ++last_id_;
                o.owner_ref = o.id;
                o.fixture = true;
                o.symbol = *look_.symbol;
                o.overlay = *look_.overlay;
                o.col = tile.col;
                o.row = tile.row;
                o.rotation = turn.value;
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

        /// The panels round a flat ride's or a ride's base's footprint. Squares count along the model's own X and Z; `gaps` are the
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

                    // Each side of the square, as the step to the neighbouring square and the quarter turns from
                    // the model's turn to the edge's. A rectangle is fenced on its rim only (both sides of one a
                    // single square across); an outline wherever the neighbour is outside it.
                    struct SquareSide
                    {
                        int dx, dz;
                        std::int32_t turns;
                    };
                    static constexpr std::array<SquareSide, 4> kSquareSides { SquareSide { -1, 0, 0 }, SquareSide { 0, 1, 1 }, SquareSide { 1, 0, 2 }, SquareSide { 0, -1, 3 } };
                    for (const SquareSide& side : kSquareSides)
                    {
                        if (inside(x + side.dx, z + side.dz))
                            continue;
                        const ModelTurn turn = ModelTurn::from(spot.direction).turned(side.turns);
                        const Tile beyond = faced_side(turn).beyond(tile);
                        if (beyond == gaps[0] || beyond == gaps[1])
                            continue;
                        out.add(tile, turn, spot.height);
                    }
                }
            }
        }

    } // namespace

    std::vector<save::PlacedObject> ride_fences(const save::Ride& ride, const ModelOf& model_of, std::uint64_t last_id)
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
        if (ride.kind == save::RideKind::Flat && ride.base.spot)
        {
            if (const ObjectModel* item = model_of(ride.base.spot->object); item && !item->water_only)
                fence_footprint(out, *ride.base.spot, *item, gaps);
        }
        if (ride.track)
        {
            for (const save::TrackPiece& piece : ride.track->pieces)
            {
                if (const ObjectModel* item = piece.scenery != 0 ? model_of(piece.scenery) : nullptr; item && item->fenced_round)
                    fence_footprint(out, { piece.scenery, piece.tile_x, piece.tile_z, piece.height, piece.direction }, *item, gaps);
            }
        }
        for (const save::Station& station : ride.stations)
        {
            for (const auto& spot : { station.entrance_spot, station.exit_spot })
            {
                if (!spot)
                    continue;
                const ModelTurn facing = ModelTurn::from(spot->direction);
                for (const ModelTurn side : { facing.turned(-1), facing.turned(1) })
                    out.add(tile_of(*spot), side, spot->height);
            }
        }
        return out.take();
    }

} // namespace rct3::park::ride
