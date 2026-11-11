// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parties: guests who go round the park together, the kinds of party that come in at the gate and the park's
// mix of them, and the step that keeps each party together. The components here, and that step.
#pragma once

#include "profiling/profiler.hpp"
#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"
#include "simulation/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// The kinds of party that come in at the gate. Children only ever come with an adult.
    // shortcut: no party of the player's own made guests (custom peeps), as players cannot make guests yet;
    // add a kind here when they can.
    enum class PartyKind : std::uint8_t
    {
        LoneTeen,
        LoneAdult,
        TeenCouple,  ///< A boy and a girl
        AdultCouple, ///< A man and a woman
        TeenGang,    ///< Teens, two or more
        AdultParty,  ///< Adults, two or more
        Family,      ///< One or two parents with their children
        DayTrip,     ///< One adult taking children out
    };
    inline constexpr std::size_t kPartyKinds = 8;

    /// The park's own mix of parties, in the registry's context, as a scenario sets it: how often each
    /// PartyKind comes against the others, and the share of families with one parent. The tuning's mix when
    /// none is there.
    struct PartyMix
    {
        std::array<float, kPartyKinds> weights { };
        Chance single_parent { };
    };

    /// Parties at the rides so far, in the registry's context: those held back from a load to ride together
    /// on the next, and those too many for one load, split over two.
    struct PartyBoarding
    {
        std::uint32_t held = 0;
        std::uint32_t split = 0;
    };

    /// Guests going round the park together, such as a family. The leader chooses where to go for all of
    /// them, by the most pressing needs among them; the others walk beside. At a ride some may stay out and
    /// wait by its exit for the rest. Now and then they stop and stand in a ring for a while.
    struct Party
    {
        entt::entity leader = entt::null;
        std::vector<entt::entity> members { }; ///< The leader among them
        std::uint32_t huddle = 0;              ///< Ticks left standing together in a ring, chatting (once all are back, too)
        NodeId gather = kNoNode;               ///< The tile it waits on for those still busy, none of another party's
        std::vector<entt::entity> away { };    ///< Members out of it for now (held up, falling, thrown, sliding), back on landing
    };

    struct Member
    {
        entt::entity party = entt::null;
    };

    /// A party member with no way over to the others: they stand looking about for `left` ticks, then are put
    /// with them.
    struct Lost
    {
        std::uint32_t left = 0;
    };

    /// How often each PartyKind comes in at the gate against the others: the park's mix (PartyMix, else the
    /// tuning's), tilted toward teens by wild rides and toward families by gentle ones, and after dark mostly
    /// teens.
    [[nodiscard]] std::array<double, kPartyKinds> party_weights(const SimWorld& world, bool night);

    /// One tick of the parties: while some members are busy the others wait near them, and once all are back
    /// they stand together a moment and go on behind the leader. Times the phase on `phase`.
    void step_parties(SimWorld& world, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
