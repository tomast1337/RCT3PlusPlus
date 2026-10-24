// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building over a track's middle: a piece picked between two others may be swapped for one that fits both, or
// taken out, which leaves a gap the track is built on into. A track lies in stretches (its segments), each a
// run of its pieces in travel order, closed when its last piece leads round into its first. A piece taken out
// of a closed stretch opens it there; one taken out of an open stretch splits it in two. Either way the
// stretch that leads up to the gap becomes the track's last, so building goes on at its open end, and a
// stretch whose start the open end reaches is joined on after it.
#pragma once

#include "park/track_pieces.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// The pieces either side of a piece along its stretch, by place in the track's pieces.
    struct PieceNeighbours
    {
        std::size_t before = 0;
        std::size_t after = 0;
    };

    /// The pieces before and after the piece `piece` of `track` along its stretch, round the ends of a closed
    /// one; none for a piece at an open end, or past the track.
    [[nodiscard]] std::optional<PieceNeighbours> piece_neighbours(const save::Track& track, std::size_t piece);

    /// Of `pieces`, those laid after `tip`, past the piece `last`, that end where the piece `next` starts
    /// (`goal`, as start_of gives it), leaving the way it goes, and whose end meets its start.
    [[nodiscard]] std::vector<LaidPiece> pieces_between(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece next, const save::TerrainGrid& grid);

    /// Takes the piece `piece` out of `track`, which must have pieces either side of it (piece_neighbours),
    /// and returns it. The pieces up to it become the track's last stretch, open at the gap; the track is no
    /// longer a circuit.
    save::TrackPiece take_out_piece(save::Track& track, std::size_t piece);

    /// Joins the stretch `stretch` of `track` on after its last one, as one open stretch.
    void join_stretch(save::Track& track, std::size_t stretch);

} // namespace rct3::park
