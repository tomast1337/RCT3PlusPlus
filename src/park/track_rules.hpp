// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a kind of tracked ride asks of its track, as its ride file sets it: which tracks may open or be tested,
// which entrances and exits it needs, and the runs of pieces it swaps for others after every change, so a
// station's ends or a waterfall's parts are always the right pieces without the player picking them.
#pragma once

#include "park/track_course.hpp"
#include "park/track_pieces.hpp"
#include "save/model/rides.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ovl
{
    struct TrackedRide;
} // namespace rct3::ovl

namespace rct3::park
{

    /// Which piece of a track a pattern step takes.
    struct PieceTest
    {
        enum class Kind : std::uint8_t
        {
            Group, ///< A piece of the group `name`; an empty name takes pieces of no group
            Piece, ///< The piece `name`
            Any,
        };
        Kind kind = Kind::Any;
        std::string name;
        bool negate = false;   ///< Takes the pieces the kind and name do not
        bool forward = false;  ///< Only a piece laid as drawn
        bool backward = false; ///< Only a piece laid backwards
        bool first = false;    ///< Only the first piece of an open track
        bool last = false;     ///< Only the last piece of an open track
    };

    /// One piece of a pattern: the piece it takes, and what that piece becomes. A `becomes` of kind Piece swaps
    /// it for that piece, laid backwards when `becomes.backward` says; any other leaves it.
    struct PatternStep
    {
        PieceTest when;
        PieceTest becomes;
    };

    using PiecePattern = std::vector<PatternStep>;

    /// What building a kind of tracked ride must keep to.
    struct TrackRules
    {
        bool never_tested = false; ///< Opens without a test run
        bool may_be_split = false; ///< Its track may be in several unjoined parts
        std::optional<std::uint32_t> min_pieces;
        bool circuit_only = false;
        /// Entrances and exits it needs: 0 one per station, 1 any number, 2 exactly one (placing another
        /// moves it), 3 at least one, 4 none.
        std::uint32_t entrances = 3;
        std::uint32_t exits = 3;
        std::vector<PiecePattern> patterns;
        /// Built from its station back towards where its riders start, as a giant slide is from its bottom up.
        bool builds_back = false;
        /// One building set down whole: its first piece is all its track, and nothing goes on after it.
        bool one_piece = false;
        bool car_rotation = false; ///< The player sets how far the cars turn on each piece
        bool car_facing = false;   ///< The player sets which way the cars face on each piece, or lets them spin
    };

    /// The rules a ride file's item stores, with names copied out of the file. A ride whose special pieces
    /// fill only the first two slots, a stack of them, is built back from its station.
    [[nodiscard]] TrackRules track_rules(const ovl::TrackedRide& ride);

    /// The rules of the tracked ride `symbol` ("<name>:trr" or the name alone) in the overlay whose common file
    /// is `file`; defaults when the file holds no such ride.
    [[nodiscard]] TrackRules load_track_rules(const std::filesystem::path& file, std::string_view symbol);

    /// The door rule that wants exactly one, which placing another of that door moves instead of adding.
    inline constexpr std::uint32_t kOneDoor = 2;

    /// Why `ride` cannot be put in `state` (open or testing) under `rules`, as a string key; nothing when it
    /// can. `editing`: its track is open in the construction panel. `course`: its track's course, when known,
    /// for the length of its stations and its reverse cable lift hill. Checked in the order the player meets them: an
    /// unjoined track, no station, too few pieces, not a circuit, a ride never tested, the panel open, a
    /// station shorter than a train, a cable lift ride without its lift, its entrances and exits, then for
    /// a ride of one entrance and one exit on an open track, an exit before the entrance.
    [[nodiscard]] std::optional<std::string> open_refusal(const save::Ride& ride, const TrackRules& rules, save::AttractionState state, bool editing, std::optional<const TrackCourse&> course = std::nullopt);

    /// `track` (pieces of `pieces` laid in order) after every pattern of `patterns` has swapped what it
    /// matches: each pattern is tried at each piece in turn, its steps taking that piece and those after.
    /// On a `circuit` no piece is first or last.
    [[nodiscard]] std::vector<LaidPiece> apply_patterns(std::span<const PieceShape> pieces, std::vector<LaidPiece> track, std::span<const PiecePattern> patterns, bool circuit);

} // namespace rct3::park
