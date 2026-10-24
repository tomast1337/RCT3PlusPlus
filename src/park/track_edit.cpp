// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_edit.hpp"

#include <algorithm>
#include <utility>

namespace rct3::park
{

    namespace
    {
        /// How near an end must come to where the next piece starts to meet it, in park units.
        constexpr float kMeet = 0.1f;

        /// A stretch with its own pieces.
        struct Stretch
        {
            save::TrackSegment segment;
            std::vector<save::TrackPiece> pieces;
        };

        /// The track's segments, one over all its pieces when it names none.
        std::vector<save::TrackSegment> segments_of(const save::Track& track)
        {
            if (!track.segments.empty())
                return track.segments;
            return { { .first_piece = 0, .piece_count = track.pieces.size(), .closed = track.circuit } };
        }

        std::vector<Stretch> stretches(const save::Track& track)
        {
            std::vector<Stretch> out;
            for (const save::TrackSegment& s : segments_of(track))
            {
                const std::size_t from = std::min(s.first_piece, track.pieces.size());
                const std::size_t to = std::min(from + s.piece_count, track.pieces.size());
                out.push_back({ s, { track.pieces.begin() + static_cast<std::ptrdiff_t>(from), track.pieces.begin() + static_cast<std::ptrdiff_t>(to) } });
            }
            return out;
        }

        /// Lays `track`'s pieces out again as `parts` hold them, in their order.
        void lay_out(save::Track& track, std::vector<Stretch> parts)
        {
            track.pieces.clear();
            track.segments.clear();
            for (Stretch& part : parts)
            {
                part.segment.first_piece = track.pieces.size();
                part.segment.piece_count = part.pieces.size();
                for (save::TrackPiece& p : part.pieces)
                {
                    p.segment = static_cast<std::uint32_t>(track.segments.size());
                    track.pieces.push_back(std::move(p));
                }
                track.segments.push_back(part.segment);
            }
            track.circuit = !track.segments.empty() && std::ranges::all_of(track.segments, &save::TrackSegment::closed);
        }

        /// The stretch holding the piece `piece`, and its place in it.
        std::optional<std::pair<std::size_t, std::size_t>> find_piece(const std::vector<save::TrackSegment>& segments, std::size_t piece)
        {
            for (std::size_t s = 0; s < segments.size(); ++s)
                if (piece >= segments[s].first_piece && piece < segments[s].first_piece + segments[s].piece_count)
                    return std::pair { s, piece - segments[s].first_piece };
            return std::nullopt;
        }
    } // namespace

    std::optional<PieceNeighbours> piece_neighbours(const save::Track& track, std::size_t piece)
    {
        const std::vector<save::TrackSegment> segments = segments_of(track);
        const auto at = piece < track.pieces.size() ? find_piece(segments, piece) : std::nullopt;
        if (!at)
            return std::nullopt;
        const save::TrackSegment& s = segments[at->first];
        const std::size_t count = std::min(s.piece_count, track.pieces.size() - s.first_piece);
        const bool first = at->second == 0;
        const bool last = at->second + 1 == count;
        // A loop of fewer than three has no piece with two others either side.
        if ((!s.closed && (first || last)) || count < 3)
            return std::nullopt;
        return PieceNeighbours { s.first_piece + (first ? count - 1 : at->second - 1), s.first_piece + (last ? 0 : at->second + 1) };
    }

    std::vector<LaidPiece> pieces_between(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece next, const save::TerrainGrid& grid)
    {
        std::vector<LaidPiece> out;
        if (next.index >= pieces.size())
            return out;
        const PieceShape& after = pieces[next.index];
        for (const LaidPiece& l : pieces_after(pieces, tip.end, last))
        {
            const PieceShape& shape = pieces[l.index];
            const auto placed = place_after(tip, shape, l.reversed, grid);
            if (placed && glm::length(placed->next.position - goal.position) < kMeet && placed->next.end == goal.end && shape.record.side == after.record.side && ends_meet(laid_end_join(shape, l.reversed), laid_start_join(after, next.reversed)))
                out.push_back(l);
        }
        return out;
    }

    save::TrackPiece take_out_piece(save::Track& track, std::size_t piece)
    {
        std::vector<Stretch> parts = stretches(track);
        const auto at = find_piece(segments_of(track), piece);
        Stretch part = std::move(parts.at(at.value().first));
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(at->first));
        const auto cut = part.pieces.begin() + static_cast<std::ptrdiff_t>(at->second);
        save::TrackPiece out = *cut;
        if (part.segment.closed)
        {
            // Round from the piece after it to the piece before it.
            std::rotate(part.pieces.begin(), cut + 1, part.pieces.end());
            part.pieces.pop_back();
            part.segment.closed = false;
            parts.push_back(std::move(part));
        }
        else
        {
            // The pieces after it stand alone, just before those leading up to it.
            Stretch after { .segment = { .closed = false }, .pieces = { cut + 1, part.pieces.end() } };
            part.pieces.erase(cut, part.pieces.end());
            if (!after.pieces.empty())
                parts.push_back(std::move(after));
            if (!part.pieces.empty())
                parts.push_back(std::move(part));
        }
        lay_out(track, std::move(parts));
        return out;
    }

    void join_stretch(save::Track& track, std::size_t stretch)
    {
        std::vector<Stretch> parts = stretches(track);
        if (stretch + 1 >= parts.size())
            return;
        Stretch joined = std::move(parts[stretch]);
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(stretch));
        parts.back().pieces.append_range(std::move(joined.pieces));
        parts.back().segment.closed = false;
        lay_out(track, std::move(parts));
    }

    void make_last(save::Track& track, std::size_t stretch)
    {
        std::vector<Stretch> parts = stretches(track);
        if (stretch + 1 >= parts.size())
            return;
        std::rotate(parts.begin() + static_cast<std::ptrdiff_t>(stretch), parts.begin() + static_cast<std::ptrdiff_t>(stretch) + 1, parts.end());
        lay_out(track, std::move(parts));
    }

    std::size_t add_piece(save::Track& track, std::optional<std::size_t> stretch, save::TrackPiece piece)
    {
        std::vector<Stretch> parts = stretches(track);
        if (!stretch || *stretch >= parts.size())
        {
            stretch = parts.empty() ? 0 : parts.size() - 1;
            parts.insert(parts.begin() + static_cast<std::ptrdiff_t>(*stretch), Stretch { });
        }
        parts[*stretch].pieces.push_back(std::move(piece));
        lay_out(track, std::move(parts));
        return *stretch;
    }

} // namespace rct3::park
