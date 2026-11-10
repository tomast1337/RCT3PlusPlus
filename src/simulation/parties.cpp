// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/parties.hpp"

#include "simulation/components.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        /// Where a party waits for those busy at an attraction. `starts` holds first their way out, then where the
        /// others stand. The spot is a footpath tile every one of `starts` walks to (past no-entry signs only the way
        /// they allow) that is not a queue, has no queue beside it and is not `taken` by another party: the nearest
        /// such to the way out, never the way out itself while there is another; failing that the nearest to one of
        /// the others; failing that the nearest tile off the queues to the way out; failing that the way out.
        NodeId gather_spot(const PathNetwork& paths, const std::vector<bool>& taken, std::span<const NodeId> starts)
        {
            const auto& nodes = paths.nodes();
            const NodeId from = starts.front();
            const auto reached = [&](NodeId n) { return std::ranges::all_of(starts, [&](NodeId s) { return s == n || paths.route(s, n).has_value(); }); };
            NodeId fallback = kNoNode;
            for (const NodeId origin : starts)
            {
                std::vector<NodeId> seen { origin };
                for (std::size_t k = 0; k < seen.size() && seen.size() <= kTuning.walking.gather_reach; ++k)
                {
                    const NodeId n = seen[k];
                    if (n != from && !nodes[n].queue)
                    {
                        const bool by_queue = std::ranges::any_of(nodes[n].next, [&](NodeId m) { return m != kNoNode && nodes[m].queue; });
                        if (!by_queue && !taken[n] && reached(n))
                            return n;
                        fallback = fallback == kNoNode ? n : fallback;
                    }
                    for (const NodeId m : nodes[n].next)
                        if (m != kNoNode && !nodes[m].queue && !nodes[m].removed && !std::ranges::contains(seen, m) && paths.may_step(n, m, Walker::Guest))
                            seen.push_back(m);
                }
            }
            return fallback == kNoNode ? from : fallback;
        }
    } // namespace

    std::array<double, kPartyKinds> party_weights(const SimWorld& world, bool night)
    {
        const entt::registry& r = world.registry;
        const auto* mix = r.ctx().find<PartyMix>();
        std::array<double, kPartyKinds> weights { };
        for (std::size_t k = 0; k < kPartyKinds; ++k)
            weights[k] = mix != nullptr ? mix->weights[k] : kTuning.parties.weights[k];
        // How wild the open rides are, from -1 (all the gentlest) to 1 (all the wildest); none tilt nothing.
        float wild = 0;
        std::size_t rides = 0;
        for (const auto [e, ride, at] : r.view<const CycleRide, const Attraction>().each())
        {
            if (!at.open)
                continue;
            wild += ride.intensity.value * 2 - 1;
            ++rides;
        }
        const double tilt = std::pow(1 + kTuning.parties.pull, rides > 0 ? wild / static_cast<float>(rides) : 0.0f);
        // shortcut: no campaign aims at a kind of party, as advertising campaigns are not simulated yet (the
        // finances work, #98); when they are, one aimed at families or at teens multiplies those weights here.
        for (std::size_t k = 0; k < kPartyKinds; ++k)
        {
            const auto kind = static_cast<PartyKind>(k);
            const bool teens = kind == PartyKind::LoneTeen || kind == PartyKind::TeenCouple || kind == PartyKind::TeenGang;
            if (teens)
                weights[k] *= tilt;
            else if (kind == PartyKind::Family || kind == PartyKind::DayTrip)
                weights[k] /= tilt;
            if (night && !teens)
                weights[k] *= kTuning.parties.night_others.value;
        }
        return weights;
    }

    void step_parties(SimWorld& world, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        const float now = static_cast<float>(world.tick) * kTickSeconds;

        phase.emplace("Parties");
        // 5. Parties: while some are busy (in a ride's queue or on it, at a stall's counter, sitting on a bench), the
        // others walk to a tile near there and wait; once all are back they stand together a moment, then go on
        // behind the leader.
        std::vector<bool> taken(paths.nodes().size(), false); // Tiles parties wait on
        for (const auto [e, party] : r.view<const Party>().each())
            if (party.gather < taken.size())
                taken[party.gather] = true;
        for (const entt::entity e : sorted(r.view<Party>()))
        {
            Party& party = r.get<Party>(e);
            std::erase_if(party.away, [&](entt::entity m) { return !r.valid(m) || !r.any_of<Held, Dropped>(m); }); // Gone some other way
            if (party.members.empty())
                continue;
            entt::entity busy_at = entt::null; // The ride, stall or bench sitter keeping them
            NodeId near = kNoNode;             // And the tile the rest wait by
            std::uint8_t off_at = 0;           // The station those on a ride get off at
            bool waiting = false;
            for (const entt::entity m : party.members)
            {
                Errand& errand = r.get<Errand>(m);
                if (errand.kind == ErrandKind::Buying && (errand.rest == 0 || --errand.rest == 0))
                    errand = { .kind = ErrandKind::Waiting, .target = errand.target };
                if ((errand.kind == ErrandKind::Queueing || errand.kind == ErrandKind::Riding) && r.valid(errand.target) && r.all_of<CycleRide>(errand.target))
                {
                    busy_at = errand.target;
                    off_at = errand.off;
                    near = exit_of(r, busy_at, off_at);
                }
                else if (errand.kind == ErrandKind::Swimming && r.valid(errand.target) && r.all_of<Attraction>(errand.target))
                {
                    busy_at = errand.target;
                    near = r.get<Attraction>(errand.target).entrance;
                }
                else if (errand.kind == ErrandKind::Buying || (r.all_of<Linger>(m) && r.get<Linger>(m).at == PathItemKind::Bench))
                {
                    busy_at = errand.kind == ErrandKind::Buying ? errand.target : m;
                    near = r.get<Stride>(m).from;
                }
                waiting = waiting || errand.kind == ErrandKind::Waiting;
            }
            // One held up or fallen away from them: they wait near the leader till that one lands and comes over.
            if (!party.away.empty() && party.leader != entt::null && busy_at == entt::null)
            {
                busy_at = party.away.front();
                const auto& lead = r.get<Stride>(party.leader);
                near = lead.progress.value < 0.5f ? lead.from : lead.to;
            }
            if (near != kNoNode && party.gather >= taken.size())
            {
                // Where each one comes from: the way out for those still busy, else the tile they are on.
                std::vector<NodeId> starts { near };
                for (const entt::entity m : party.members)
                {
                    const ErrandKind kind = r.get<Errand>(m).kind;
                    const auto& stride = r.get<Stride>(m);
                    const NodeId at = stride.progress.value < 0.5f ? stride.from : stride.to;
                    if (kind != ErrandKind::Queueing && kind != ErrandKind::Riding && kind != ErrandKind::Swimming && kind != ErrandKind::Buying && !r.all_of<Linger>(m) && at < paths.nodes().size() && !std::ranges::contains(starts, at))
                        starts.push_back(at);
                }
                party.gather = gather_spot(paths, taken, starts);
                taken[party.gather] = true;
            }
            if (busy_at != entt::null || (waiting && party.gather < taken.size()))
            {
                const NodeId spot = party.gather;
                bool back = busy_at == entt::null; // All standing with the others, or where they cannot leave
                bool cut_off = false;              // One off the queues finds no way to the spot: look for another
                NodeId come_to = kNoNode;          // Or the tile of one the others can walk to, though not back
                for (const entt::entity m : party.members)
                {
                    auto& errand = r.get<Errand>(m);
                    auto& stride = r.get<Stride>(m);
                    if (r.all_of<Linger>(m))
                        continue; // Sitting it out
                    // Lost long enough, they are put with the others once there is a spot to wait on: the one place
                    // this happens.
                    // shortcut: put there in plain view; look for a moment the camera is elsewhere if it shows.
                    if (auto* lost = r.try_get<Lost>(m); lost != nullptr && (lost->left == 0 || --lost->left == 0) && spot < paths.nodes().size())
                    {
                        r.remove<Lost>(m);
                        stride.route.clear();
                        stride.from = spot;
                        stride.to = spot;
                        stride.progress.value = 1;
                        stride.lane_from = stride.lane_to = glm::vec2 { 0.0f };
                        place(paths, stride, r.get<Pose>(m));
                        errand = { .kind = ErrandKind::Waiting, .target = busy_at, .off = off_at };
                    }
                    // Those going to wait, and those done, who walk over to the others.
                    const bool still = errand.kind == ErrandKind::Waiting && stride.route.empty() && (stride.progress.value >= 1 || stride.from == stride.to);
                    // One who found no way over waits a while before looking again (rest counts it down).
                    const bool joins = errand.kind == ErrandKind::Following || (m == party.leader && errand.kind == ErrandKind::Wandering) || (still && stride.to != spot && errand.rest == 0);
                    if (!joins)
                    {
                        back = back && still && !r.all_of<Lost>(m);
                        continue;
                    }
                    const NodeId at = stride.progress.value < 0.5f ? stride.from : stride.to;
                    // No way over (behind a no-entry sign): at least off the queue, to wait beside it. Off it, if the
                    // others can walk to them they come; else they are lost, and stand looking about for a while.
                    auto way = paths.route(at, spot);
                    const bool over = way || paths.nodes()[at].queue; // On the way, or where they cannot leave: as good as back
                    if (way)
                        r.remove<Lost>(m);
                    else if (!paths.nodes()[at].queue)
                    {
                        cut_off = true;
                        if (paths.route(spot, at))
                            come_to = at;
                        else if (!r.all_of<Lost>(m))
                        {
                            r.emplace<Lost>(m, ticks(kTuning.walking.lost_wait.value));
                            think(r, m, ThoughtKind::Lost, entt::null, now);
                        }
                    }
                    if (!way)
                        way = paths.way_out(at);
                    stride.route = std::move(way).value_or(std::vector<NodeId> { });
                    stride.from = at;
                    stride.to = at;
                    stride.progress.value = 1;
                    stride.lane_to = stride.lane_from;
                    back = back && stride.route.empty() && over;
                    if (errand.kind != ErrandKind::Waiting)
                        errand = { .kind = ErrandKind::Waiting, .target = busy_at, .off = off_at };
                    if (stride.route.empty() && at != spot)
                        errand.rest = ticks(kTuning.walking.retry_way.value);
                }
                // The others go over to the one they can reach; else, while some are busy, a spot is chosen again
                // next tick from where they all are now.
                if (come_to != kNoNode)
                    party.gather = come_to;
                else if (cut_off && busy_at != entt::null)
                    party.gather = kNoNode;
                if (!back)
                    party.huddle = 0;
                else if (party.huddle == 0)
                    party.huddle = ticks(kTuning.walking.regroup.value);
                if (!back || --party.huddle > 0)
                    continue;
            }
            if (waiting)
            {
                party.gather = kNoNode;
                const Errand was = r.get<Errand>(party.leader);
                for (const entt::entity m : party.members)
                {
                    auto& errand = r.get<Errand>(m);
                    if (m == party.leader)
                        errand = errand.kind == ErrandKind::Waiting ? Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) } : errand;
                    else
                        errand = { ErrandKind::Following, entt::null, 0 };
                }
                // A party riding across the park walks on from the exit where it was going.
                if (auto* crossing = r.try_get<Crossing>(party.leader); crossing != nullptr && was.kind == ErrandKind::Waiting && was.target == crossing->ride)
                {
                    auto& stride = r.get<Stride>(party.leader);
                    const NodeId at = stride.progress.value < 0.5f ? stride.from : stride.to;
                    if (auto back = r.valid(was.target) && r.all_of<CycleRide>(was.target) ? paths.route(at, exit_of(r, was.target, was.off)) : std::nullopt)
                    {
                        back->insert(back->end(), crossing->route.begin(), crossing->route.end());
                        stride.route = std::move(*back);
                        stride.from = at;
                        stride.to = at;
                        stride.progress.value = 1;
                        stride.lane_to = stride.lane_from;
                        r.get<Errand>(party.leader) = crossing->errand;
                    }
                    r.remove<Crossing>(party.leader);
                }
            }
        }
    }

} // namespace rct3::simulation
