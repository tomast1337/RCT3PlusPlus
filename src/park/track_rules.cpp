// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_rules.hpp"

#include "core/maybe.hpp"
#include "ovl/overlay.hpp"
#include "ovl/tracked_ride.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <format>
#include <ranges>

namespace rct3::park
{

    namespace
    {
        // Bits of a ride file's attraction flags.
        constexpr std::uint32_t kNeverTested = 0x10 | 0x4000;
        constexpr std::uint32_t kMayBeSplit = 0x200;

        PieceTest piece_test(const ovl::PieceTest& t)
        {
            const auto kind = t.kind == 0 ? PieceTest::Kind::Group : t.kind == 1 ? PieceTest::Kind::Piece : PieceTest::Kind::Any;
            return { kind, std::string(t.name), t.negate, t.forward, t.backward, t.first, t.last };
        }

        /// Whether the piece at `at` of `track` passes `test`.
        bool passes(std::span<const PieceShape> pieces, std::span<const LaidPiece> track, std::size_t at, const PieceTest& test, bool circuit)
        {
            const PieceRecord& r = pieces[track[at].index].record;
            bool named = true;
            if (test.kind == PieceTest::Kind::Piece)
                named = strings::equals_ignoring_case(r.name, test.name);
            else if (test.kind == PieceTest::Kind::Group)
                named = strings::equals_ignoring_case(r.group, test.name);
            if (named == test.negate)
                return false;
            if ((test.first && (circuit || at != 0)) || (test.last && (circuit || at + 1 != track.size())))
                return false;
            const bool backward = track[at].reversed;
            return !(test.forward && backward) && !(test.backward && !backward);
        }
    } // namespace

    TrackRules track_rules(const ovl::TrackRules& stored)
    {
        TrackRules rules;
        rules.never_tested = (stored.flags & kNeverTested) != 0;
        rules.may_be_split = (stored.flags & kMayBeSplit) != 0;
        rules.min_pieces = stored.min_pieces;
        rules.circuit_only = stored.circuit_only;
        rules.entrances = stored.entrances;
        rules.exits = stored.exits;
        for (const auto& pattern : stored.patterns)
        {
            auto& copied = rules.patterns.emplace_back();
            for (const ovl::PatternEntry& e : pattern)
                copied.push_back({ piece_test(e.when), piece_test(e.becomes) });
        }
        return rules;
    }

    TrackRules load_track_rules(const std::filesystem::path& file, std::string_view symbol)
    {
        const std::string_view name = symbol.substr(0, symbol.find(':'));
        auto overlay = ovl::Overlay::load(file);
        if (!overlay)
            return { };
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TrackedRideItem& item : ovl::read_tracked_rides(*overlay, *part))
            {
                if (item.ride && strings::equals_ignoring_case(item.name, name))
                    return track_rules(item.ride->rules);
            }
        }
        return { };
    }

    std::optional<std::string> open_refusal(const save::Ride& ride, const TrackRules& rules, save::AttractionState state, bool editing)
    {
        if (state == save::AttractionState::Closed || ride.kind != save::RideKind::Tracked)
            return std::nullopt;
        const bool test = state == save::AttractionState::Testing;
        const std::size_t pieces = ride.track ? ride.track->pieces.size() : 0;
        const bool circuit = ride.track && ride.track->circuit;
        if (ride.track && ride.track->segments.size() > 1 && !rules.may_be_split && ride.mode != save::OperatingMode::WaterTransport)
            return "TrackedRideIsDisjoint";
        if (ride.stations.empty())
            return "RideHasNoStation";
        if (rules.min_pieces && pieces < *rules.min_pieces)
            return "CannotOpenOrTestInsufficientPieces";
        if (rules.circuit_only && !circuit)
            return "CannotOpenOrTestNotCircuit";
        if (test && rules.never_tested)
            return "CannotTestTrackedRide";
        if (!test && editing)
            return "CannotOpenTrackedRideWhileEditing";
        // shortcut: no check that a station is as long as a train or that a reverse cable lift ride has its
        // lift, upgrade when built tracks carry station lengths and lift pieces.
        const auto door = [&](std::uint32_t rule, auto built, std::string_view which) -> std::optional<std::string> {
            const auto placed = static_cast<std::size_t>(std::ranges::count_if(ride.stations, built));
            const bool enough = rule == 0 ? placed >= ride.stations.size() : rule == 2 || rule == 3 ? placed > 0 : true;
            if (enough)
                return std::nullopt;
            return std::format("Cant{}Ride{}{}", test ? "Test" : "Open", rule == 0 ? "Missing" : "No", which);
        };
        if (auto refused = door(rules.entrances, [](const save::Station& s) { return s.entrance != 0; }, "Entrance"))
            return refused;
        return door(rules.exits, [](const save::Station& s) { return s.exit != 0; }, "Exit");
    }

    std::vector<LaidPiece> apply_patterns(std::span<const PieceShape> pieces, std::vector<LaidPiece> track, std::span<const PiecePattern> patterns, bool circuit)
    {
        for (const PiecePattern& pattern : patterns)
        {
            for (std::size_t start = 0; !pattern.empty() && start + pattern.size() <= track.size(); ++start)
            {
                const bool matches = std::ranges::all_of(std::views::iota(std::size_t { 0 }, pattern.size()), [&](std::size_t k) { return passes(pieces, track, start + k, pattern[k].when, circuit); });
                if (!matches)
                    continue;
                for (std::size_t k = 0; k < pattern.size(); ++k)
                {
                    const PieceTest& becomes = pattern[k].becomes;
                    if (becomes.kind != PieceTest::Kind::Piece || passes(pieces, track, start + k, becomes, circuit))
                        continue;
                    // A piece that keeps its way is never laid backwards by a pattern.
                    const auto found = std::ranges::find_if(pieces, [&](const PieceShape& p) { return strings::equals_ignoring_case(p.record.name, becomes.name) && !(becomes.backward && (p.record.flags & kPieceKeepsWay) != 0); });
                    if (found != pieces.end())
                        track[start + k] = { static_cast<std::size_t>(found - pieces.begin()), becomes.backward };
                }
            }
        }
        return track;
    }

} // namespace rct3::park
