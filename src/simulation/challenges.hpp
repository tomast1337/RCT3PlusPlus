// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Challenges: side goals a scenario of the newer generations offers while it runs, each with its own time limit
// and prizes. A challenge waits for the level it names to be won (or for nothing), then for the days its file
// gives, and is offered to the player, who takes it on, declines it, or puts it off once. One challenge is offered
// or runs at a time; the others wait their turn. Taken on, it is met once all its goals hold at the same time
// before its time runs out, and the player is warned a week before it does. Met, it gives its prizes: cash, a
// research boost, and publicity, which draws more parties for some weeks.

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "simulation/objectives.hpp"
#include "simulation/park/messages.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace rct3::simulation
{
    struct Research;
    namespace park
    {
        struct ParkState;
    } // namespace park

    /// Game seconds in a day, as challenges count their waits and time limits.
    inline constexpr f32 kChallengeDay = save::model::GameClock::kUnitsPerWeek / 7;

    /// How the player answers a challenge offered.
    enum class ChallengeAnswer : u8
    {
        Accept,
        Decline,
        Postpone,
    };

    /// The challenge waiting on the player's answer, by its place in `set.challenges`.
    [[nodiscard]] std::optional<usize> challenge_offered(const save::model::ObjectiveSet& set);

    /// Whether challenge `c` may still be put off: only once.
    [[nodiscard]] bool may_postpone(const save::model::Challenge& c) noexcept;

    /// Answers offered challenge `which` of `set`. Taken on, its time limit starts and its goals count sales
    /// from nought.
    void answer_challenge(save::model::ObjectiveSet& set, usize which, ChallengeAnswer answer);

    /// Moves the challenges of `set` on by `elapsed` game seconds, the park standing as `figures` says: counts
    /// down to the next offer and judges the one running, posting a week's warning or a failure to `messages`.
    /// Returns the challenge just met, by its place, whose prizes are then the player's.
    std::optional<usize> step_challenges(save::model::ObjectiveSet& set, const ParkFigures& figures, f32 elapsed, park::ParkMessages& messages);

    /// Counts the sale of `item` (a carried item's symbol; empty when not known) at a stall of kind `stall` to the
    /// items-bought goals of the challenge running.
    void count_sale(save::model::ObjectiveSet& set, std::string_view stall, std::string_view item);

    /// Gives challenge `c`'s prizes: cash booked as prize money in `finance`'s ledger (`kept` as for park::book),
    /// a research boost added to `research`'s, and publicity, which draws more parties until
    /// `set.publicity_until`.
    void give_challenge_prizes(park::ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, Research& research, save::model::ObjectiveSet& set, const save::model::Challenge& c);

} // namespace rct3::simulation
