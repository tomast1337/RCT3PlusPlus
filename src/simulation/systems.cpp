// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/systems.hpp"

#include "park/terrain_mesh.hpp"
#include "profiling/profiler.hpp"
#include "simulation/animals.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/crowds.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/park/state.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/work.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/tuning.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    void think(entt::registry& r, entt::entity guest, ThoughtKind kind, entt::entity about, float now)
    {
        auto& thoughts = r.get_or_emplace<Thoughts>(guest);
        if (std::ranges::any_of(thoughts.recent, [&](const Thought& t) { return t.kind == kind && t.about == about && now - t.time.value < kTuning.thoughts.rethink.value; }))
            return;
        const auto variant = static_cast<std::uint32_t>(random::next(r.get<Dice>(guest).state));
        thoughts.recent.insert(thoughts.recent.begin(), Thought { kind, about, now, variant });
        if (thoughts.recent.size() > kTuning.thoughts.kept)
            thoughts.recent.pop_back();
        ++thoughts.had;
    }

    void stop_at(const PathNetwork& paths, NodeId node, Stride& stride, Pose& pose)
    {
        stride.from = node;
        stride.to = node;
        stride.progress.value = 1;
        stride.route.clear();
        place(paths, stride, pose);
    }

    void tell(entt::registry& r, entt::entity inspector, Finding finding, entt::entity about, float now)
    {
        auto& told = r.get_or_emplace<Rounds>(inspector).told;
        std::erase_if(told, [&](const auto& t) { return now - std::get<2>(t) >= kTuning.inspector.quiet.value; });
        if (std::ranges::any_of(told, [&](const auto& t) { return std::get<0>(t) == about && std::get<1>(t) == finding; }))
            return;
        told.emplace_back(about, finding, now);
        r.ctx().emplace<Findings>().found.emplace_back(finding, about);
    }

    Pose between_steps(const Pose& pose, float along)
    {
        const float turn = std::remainder(pose.heading.value - pose.was_heading.value, 2 * glm::pi<float>());
        const float walked = pose.walked.value + along * glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
        return { glm::mix(pose.was, pose.position, along), pose.was_heading.value + turn * along, pose.was, pose.was_heading.value, walked };
    }

    std::optional<float> cannon_turn(float heading, glm::vec2 way, float most)
    {
        const float turn = std::remainder(std::atan2(way.x, way.y) - heading, 2 * glm::pi<float>());
        if (std::abs(turn) > most)
            return std::nullopt;
        return turn;
    }

    bool is_night(float time)
    {
        return time >= kTuning.night.dusk.value || time < kTuning.night.dawn.value;
    }

    std::uint32_t ticks(float seconds)
    {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0f) / kTickSeconds));
    }

    namespace
    {
        /// The gates, litter, path items and vandals, then the staff and the crowds, for one tick: after the guests
        /// have walked and bought.
        void tidy(SimWorld& world)
        {
            entt::registry& r = world.registry;
            const PathNetwork& paths = world.paths;
            constexpr float dt = kTickSeconds;
            const float now = static_cast<float>(world.tick) * kTickSeconds;
            const Weather weather = weather_in(r);
            auto& vandalism = r.ctx().emplace<Vandalism>();
            // Guests outside the park walk to the gate, and new parties come (simulation/guests.cpp).
            step_gates(world, weather);
            vandalism.recent *= std::exp(-dt / kTuning.vandals.fade.value);
            if (vandalism.since_warning.value >= 0)
                vandalism.since_warning.value += dt;

            // What lies and stands on each tile, in entity order. Emptying a tile's list keeps its memory for the next tick.
            StepBuffers& buffers = world.buffers;
            auto& litter_at = buffers.litter_at;
            auto& items_at = buffers.items_at;
            for (auto& [node, list] : litter_at)
                list.clear();
            for (auto& [node, list] : items_at)
                list.clear();
            for (const entt::entity e : sorted(r.view<Litter>()))
            {
                auto& piece = r.get<Litter>(e);
                piece.age.value += dt;
                litter_at[piece.node].push_back(e);
            }
            auto& speakers = buffers.speakers;
            speakers.clear();
            for (const entt::entity e : sorted(r.view<const PathItem>()))
            {
                items_at[r.get<PathItem>(e).node].push_back(e);
                if (r.get<PathItem>(e).kind == PathItemKind::Speaker)
                    speakers.push_back(e);
            }
            const auto items = [&](NodeId node) -> std::span<const entt::entity> {
                const auto found = items_at.find(node);
                return found != items_at.end() ? std::span<const entt::entity>(found->second) : std::span<const entt::entity> { };
            };
            const auto litter_on = [&](NodeId node) {
                const auto found = litter_at.find(node);
                return found != litter_at.end() ? found->second.size() : 0;
            };
            // The tiles guards keep vandals off: theirs and those a few steps along the paths from it.
            auto& guarded = buffers.guarded;
            guarded.assign(paths.nodes().size(), false);
            for (const auto [e, staff, stride] : r.view<const Staff, const Stride>().each())
            {
                if (staff.role != StaffRole::Guard || stride.to >= paths.nodes().size())
                    continue;
                auto& ring = buffers.ring;
                auto& next = buffers.next;
                ring.assign(1, stride.to);
                guarded[stride.to] = true;
                for (int step = 0; step < kTuning.vandals.guard_reach; ++step)
                {
                    next.clear();
                    for (const NodeId n : ring)
                        for (const NodeId m : paths.nodes()[n].next)
                            if (m != kNoNode && !guarded[m])
                            {
                                guarded[m] = true;
                                next.push_back(m);
                            }
                    std::swap(ring, next);
                }
            }

            // Guests: glum on dirty or vandalised paths, binning or dropping their rubbish, and the angriest breaking things.
            const auto guests_view = r.view<const Errand, const Stride, Mood, Dice, const Needs>(entt::exclude<Staff>); // Not an inspector on a visit
            const bool pool_open = std::ranges::any_of(r.view<const PoolDoor, const Attraction>().each(), [](const auto& room) { return std::get<2>(room).open; });
            // Who sits on seat `seat` of bench `bench`, if anyone, and whether they are getting up.
            const auto sitter = [&](entt::entity bench, int seat) -> std::optional<const Linger&> {
                for (const auto [g, linger] : r.view<const Linger>().each())
                    if (linger.bench == bench && linger.seat == seat)
                        return linger;
                return std::nullopt;
            };
            for (const entt::entity g : sorted(guests_view))
            {
                auto [errand, stride, mood, dice, needs] = guests_view.get(g);
                if (!walking(errand.kind) || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                    continue;
                // Every so often, once the park has run a while, a guest takes in what is round them, walking or
                // stopped, unless taken up with a show: plenty of scenery, or else a tidy, unspoilt path. Each at
                // their own moment, by who they are.
                const float look = kTuning.surroundings.look.value;
                const float phase = static_cast<float>(entt::to_integral(g) % 101) / 101 * look;
                if (now > look && r.all_of<Visitor>(g) && !r.any_of<Watching, Gesture>(g) && std::floor((now + phase) / look) != std::floor((now - dt + phase) / look))
                {
                    const NodeId here = stride.from;
                    const auto* near = r.ctx().find<SceneryNear>();
                    const bool clean = litter_on(here) == 0 && std::ranges::none_of(paths.nodes()[here].next, [&](NodeId m) { return m != kNoNode && litter_on(m) > 0; });
                    const bool whole = std::ranges::none_of(items(here), [&](entt::entity i) { return r.get<PathItem>(i).broken; });
                    if (near != nullptr && here < near->by_node.size() && near->by_node[here] >= kTuning.surroundings.great)
                    {
                        think(r, g, ThoughtKind::GreatScenery, entt::null, now);
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.great_cheer.value);
                    }
                    else if (clean && whole)
                    {
                        think(r, g, ThoughtKind::ReallyTidy, entt::null, now);
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.tidy_cheer.value);
                    }
                }
                // Sitting rests a guest, watching cheers them; then they walk on.
                if (auto* linger = r.try_get<Linger>(g))
                {
                    linger->left.value -= dt;
                    if (linger->at != PathItemKind::Bench)
                    {
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.facilities.sight_cheer.value * dt);
                        // Firing a water cannon: now and then they swing it on to another guest in reach, who
                        // gets soaked while it is on them.
                        if (linger->at == PathItemKind::WaterCannon && r.all_of<Pose>(g))
                        {
                            const glm::vec3 from = r.get<Pose>(g).position;
                            const float reach = kTuning.surroundings.cannon_reach.value;
                            // Only those in front of it: it turns so far, and never back on its firer.
                            const auto* cannon = r.valid(linger->bench) ? r.try_get<PathItem>(linger->bench) : nullptr;
                            const auto in_reach = [&](entt::entity t) {
                                if (t == g || cannon == nullptr || !r.valid(t) || !r.all_of<Pose, Mood, Visitor>(t) || r.any_of<Staff>(t))
                                    return false;
                                const glm::vec3 to = r.get<Pose>(t).position - from;
                                return glm::length(to) <= reach && cannon_turn(cannon->heading, { to.x, to.z }, kTuning.surroundings.cannon_swing).has_value();
                            };
                            if (!in_reach(linger->target) || random::chance(dice.state, kTuning.surroundings.retarget.value * dt))
                            {
                                std::vector<entt::entity> near;
                                for (const entt::entity t : r.view<const Pose, const Visitor>())
                                    if (in_reach(t))
                                        near.push_back(t);
                                std::ranges::sort(near);
                                linger->target = near.empty() ? entt::null : near[random::below(dice.state, near.size())];
                            }
                            if (r.valid(linger->target))
                            {
                                Mood& soaked = r.get<Mood>(linger->target);
                                soaked.wet.value = clamp01(soaked.wet.value + kTuning.surroundings.soaking.value * dt);
                            }
                        }
                        if (linger->left.value <= 0)
                            r.remove<Linger>(g);
                        continue;
                    }
                    mood.energy.value = clamp01(mood.energy.value + kTuning.facilities.rest.value * dt);
                    mood.nausea.value = clamp01(mood.nausea.value - kTuning.sickness.rest_settle.value * dt);
                    linger->sat.value += dt;
                    if (linger->rising)
                    {
                        if (linger->left.value <= 0)
                            r.remove<Linger>(g);
                        continue;
                    }
                    // Up once no longer worn out or sick and done eating, the more rested the sooner; someone beside them
                    // keeps them chatting.
                    const bool needy = mood.energy.value < kTuning.facilities.tired.value || mood.nausea.value >= kTuning.needs.want.value || munching(r, g);
                    const auto mate = [&](int seat) { return r.valid(linger->bench) && seat >= 0 && sitter(linger->bench, seat) && !sitter(linger->bench, seat)->rising; };
                    const float chat = mate(linger->seat - 1) || mate(linger->seat + 1) ? kTuning.facilities.chatting : 1;
                    if (linger->left.value <= 0 && !needy && random::chance(dice.state, kTuning.facilities.stand.value * dt * mood.energy.value / chat))
                    {
                        linger->rising = true;
                        linger->left = kTuning.facilities.rise;
                    }
                    continue;
                }
                // What a guest just come to a tile thinks of themselves and of the path.
                if (stride.fresh)
                {
                    const auto about = [&](ThoughtKind kind, bool when) {
                        if (when)
                            think(r, g, kind, entt::null, now);
                    };
                    about(ThoughtKind::Hungry, needs.hunger.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Thirsty, needs.thirst.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Toilet, needs.bladder.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Tired, mood.energy.value < kTuning.facilities.tired.value);
                    about(mood.nausea.value >= kTuning.sickness.sick.value ? ThoughtKind::VerySick : ThoughtKind::Sick, mood.nausea.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Broke, r.get<Wallet>(g).cash.value < kTuning.facilities.low_cash.value);
                    about(ThoughtKind::Litter, litter_on(stride.from) >= kTuning.litter.dirty_path);
                    about(ThoughtKind::Raining, weather.rain && !r.all_of<Umbrella>(g));
                    about(ThoughtKind::Soaked, mood.wet.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Hot, weather.temperature.value >= kTuning.weather.hot.value || mood.heat.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Thunder, weather.storm);
                    about(ThoughtKind::GoHome, errand.kind == ErrandKind::Heading && r.valid(errand.target) && r.all_of<ParkGate>(errand.target));
                    about(ThoughtKind::Cold, weather.temperature.value < kTuning.weather.cold.value);
                    const bool swim = needs.swim.value >= kTuning.needs.want.value;
                    about(ThoughtKind::NoPools, swim && !pool_open);
                    about(ThoughtKind::TooColdForPools, swim && pool_open && needs.swim.value < swim_wish_needed(weather.temperature.value));
                    about(ThoughtKind::Vandalism, std::ranges::any_of(items(stride.from), [&](entt::entity i) { return r.get<PathItem>(i).broken; }));
                    const auto here = litter_at.find(stride.from);
                    about(ThoughtKind::Vomit, here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }));
                    // Within earshot of a loudspeaker a guest may like its music, once in a while whichever it is.
                    // shortcut: every speaker plays, and its music always pleases; upgrade when speakers keep their
                    // music and on/off state.
                    const PathTileSpot& tile = paths.nodes()[stride.from].tile;
                    const auto heard = std::ranges::find_if(speakers, [&](entt::entity s) {
                        const PathTileSpot& at = paths.nodes()[r.get<PathItem>(s).node].tile;
                        return std::abs(at.col - tile.col) <= kTuning.surroundings.earshot && std::abs(at.row - tile.row) <= kTuning.surroundings.earshot;
                    });
                    const auto* thoughts = r.try_get<Thoughts>(g);
                    const bool lately = thoughts != nullptr && std::ranges::any_of(thoughts->recent, [&](const Thought& t) { return t.kind == ThoughtKind::NiceMusic && now - t.time.value < kTuning.thoughts.rethink.value; });
                    if (heard != speakers.end() && !lately && random::chance(dice.state, kTuning.surroundings.music_thought.value))
                        think(r, g, ThoughtKind::NiceMusic, *heard, now);
                }
                // A wandering guest just come to a tile may sit on a bench there when tired or eating, or stop at a sight.
                if (errand.kind == ErrandKind::Wandering && stride.fresh)
                {
                    const NodeId here = stride.from;
                    const auto sight = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Sight || r.get<PathItem>(i).kind == PathItemKind::PhotoSpot; });
                    // Jumping fountains catch a passer's eye for a moment, and often please them.
                    if (const auto fountain = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Fountain; }); fountain != items(here).end() && random::chance(dice.state, kTuning.surroundings.glance.value))
                    {
                        r.emplace_or_replace<Glance>(g, Glance { .at = *fountain, .left = kTuning.surroundings.glancing });
                        if (random::chance(dice.state, kTuning.surroundings.fountain_thought.value))
                        {
                            think(r, g, ThoughtKind::JumpingFountainsGood, entt::null, now);
                            mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.fountain_cheer.value);
                        }
                        continue;
                    }
                    // A free water cannon tempts some passers to fire it a while, one at a time.
                    const auto cannon = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::WaterCannon && !r.get<PathItem>(i).broken; });
                    if (cannon != items(here).end() && std::ranges::none_of(r.view<const Linger>().each(), [&](const auto& other) { return std::get<1>(other).bench == *cannon; }) && random::chance(dice.state, kTuning.surroundings.fire_cannon.value))
                    {
                        r.emplace<Linger>(g, Linger { .at = PathItemKind::WaterCannon, .left = kTuning.surroundings.firing, .bench = *cannon });
                        continue;
                    }
                    // The first free seat of a whole bench on their tile.
                    const auto seat = [&]() -> std::optional<std::pair<entt::entity, std::uint8_t>> {
                        for (const entt::entity i : items(here))
                            if (const auto& item = r.get<PathItem>(i); item.kind == PathItemKind::Bench && !item.broken)
                                for (std::uint8_t s = 0; s < item.seats; ++s)
                                    if (!sitter(i, s))
                                        return std::pair { i, s };
                        return std::nullopt;
                    };
                    // Worn out, sick, or with something to eat or drink in hand.
                    if (mood.energy.value < kTuning.facilities.tired.value || mood.nausea.value >= kTuning.needs.want.value || munching(r, g))
                        if (const auto free = seat(); free && random::chance(dice.state, kTuning.facilities.sit_chance.value))
                        {
                            r.emplace<Linger>(g, Linger { .at = PathItemKind::Bench, .left = kTuning.facilities.sit, .bench = free->first, .seat = free->second });
                            continue;
                        }
                    // Most walk past an effect or animatronic, taking it in as scenery; a view is worth a stop.
                    if (sight != items(here).end() && random::chance(dice.state, r.get<PathItem>(*sight).kind == PathItemKind::Sight ? kTuning.surroundings.stop.value : kTuning.facilities.watch_chance.value))
                    {
                        const PathItemKind kind = r.get<PathItem>(*sight).kind;
                        r.emplace<Linger>(g, Linger { .at = kind, .left = kTuning.facilities.watch, .bench = *sight });
                        // At a view the whole party stops to take it in, in a row along it, and judges it by the scenery
                        // round it: a fine one pleases them, a bare one gets folded arms. The player keeps a photo of each
                        // stop their own guest's party makes, one of the others taking it.
                        if (kind == PathItemKind::PhotoSpot)
                        {
                            const auto* member = r.try_get<Member>(g);
                            const std::vector<entt::entity> party = member != nullptr ? r.get<Party>(member->party).members : std::vector { g };
                            const auto* near = r.ctx().find<SceneryNear>();
                            const bool fine = near != nullptr && here < near->by_node.size() && near->by_node[here] >= kTuning.surroundings.great;
                            entt::entity avatar = entt::null;
                            for (const entt::entity m : party)
                            {
                                if (avatar == entt::null && r.get<Visitor>(m).avatar)
                                    avatar = m;
                                if (!r.all_of<Mood, Stride>(m))
                                    continue;
                                if (m != g && !r.all_of<Linger>(m))
                                    r.emplace<Linger>(m, Linger { .at = kind, .left = kTuning.facilities.watch, .bench = *sight });
                                if (!fine)
                                {
                                    r.emplace_or_replace<Gesture>(m, GestureKind::Disapproval, kTuning.gestures.shown.value);
                                    continue;
                                }
                                think(r, m, ThoughtKind::PhotoSpotGood, entt::null, now);
                                auto& happiness = r.get<Mood>(m).happiness.value;
                                happiness = clamp01(happiness + kTuning.surroundings.view_cheer.value);
                            }
                            if (avatar != entt::null)
                            {
                                r.ctx().emplace<Photos>().taken.push_back({ avatar, here });
                                think(r, avatar, ThoughtKind::Photographee, entt::null, now);
                            }
                            // One of a party films the rest: the player's guest's party has their photo taken.
                            const entt::entity posing = avatar != entt::null ? avatar : g;
                            if (const auto other = std::ranges::find_if(party, [&](entt::entity m) { return m != posing; }); other != party.end())
                            {
                                if (avatar != entt::null)
                                    think(r, *other, ThoughtKind::Photographer, entt::null, now);
                                if (auto* taking = r.try_get<Linger>(*other); taking != nullptr && taking->bench == *sight)
                                    taking->camera = true;
                            }
                        }
                        continue;
                    }
                }
                const NodeId at = stride.progress.value < 0.5f ? stride.from : stride.to;
                if (litter_on(at) >= kTuning.litter.dirty_path)
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.litter.dirt_gloom.value * dt);
                if (const auto here = litter_at.find(at); here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }))
                {
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.sickness.vomit_gloom.value * dt);
                    mood.nausea.value = clamp01(mood.nausea.value + kTuning.sickness.queasy.value * dt);
                }
                if (mood.nausea.value >= kTuning.sickness.sick.value && random::chance(dice.state, kTuning.sickness.sick_walking.value * dt))
                {
                    litter_at[at].push_back(be_sick(r, g, at));
                }
                if (std::ranges::any_of(items(at), [&](entt::entity i) { return r.get<PathItem>(i).broken; }))
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.vandals.gloom.value * dt);
                // Empties go in a bin with room on their tile, all they have, as many as fit; one held too long goes
                // in a pocket, or on the path once both are full.
                if (auto* rubbish = r.try_get<Carried>(g); rubbish != nullptr && (rubbish->bites == 0 || !rubbish->held))
                {
                    const bool in_hand = rubbish->held && rubbish->bites == 0;
                    if (in_hand)
                        rubbish->since.value += dt;
                    const auto bin = std::ranges::find_if(items(at), [&](entt::entity i) { return r.all_of<Bin>(i) && !r.get<PathItem>(i).broken && r.get<Bin>(i).contents + 1 <= r.get<Bin>(i).capacity; });
                    if (bin != items(at).end())
                    {
                        auto& b = r.get<Bin>(*bin);
                        for (bool hand = in_hand; (hand || rubbish->pocketed > 0) && b.contents + 1 <= b.capacity; hand = false)
                        {
                            b.contents += 1;
                            if (hand)
                                rubbish->held = false;
                            else
                                --rubbish->pocketed;
                        }
                        r.emplace_or_replace<Gesture>(g, Gesture { .kind = GestureKind::HandOver, .left = kTuning.gestures.shown, .at = *bin });
                        if (!rubbish->held && rubbish->pocketed == 0)
                            r.remove<Carried>(g);
                    }
                    else if (in_hand && rubbish->since.value >= kTuning.litter.carry.value)
                        free_hand(r, g, *rubbish, at);
                }
                if (mood.happiness.value < kTuning.vandals.mood.value && !guarded[at] && random::chance(dice.state, kTuning.vandals.breaking.value * dt))
                {
                    const auto whole = std::ranges::find_if(items(at), [&](entt::entity i) { return !r.get<PathItem>(i).broken && breakable(r.get<PathItem>(i).kind); });
                    if (whole != items(at).end())
                    {
                        r.get<PathItem>(*whole).broken = true;
                        ++vandalism.broken;
                        vandalism.recent += 1;
                        if (vandalism.recent >= kTuning.vandals.bad && (vandalism.since_warning.value < 0 || vandalism.since_warning.value >= kTuning.vandals.warning_every.value))
                        {
                            ++vandalism.warnings;
                            vandalism.since_warning.value = 0;
                        }
                    }
                }
            }

            // Staff: paid, at their posts and walking the paths at their jobs (simulation/staff/work.cpp).
            staff::step_work(world);

            // Crowds round entertainers and VIPs (simulation/crowds.cpp).
            step_crowds(world);
        }
    } // namespace

    Replacement replacement(const SimWorld& world, const std::function<std::int64_t(std::uint64_t object)>& price)
    {
        Replacement out;
        for (const auto [e, item] : world.registry.view<const PathItem>().each())
            if (item.broken)
            {
                ++out.items;
                out.cost += price(item.object);
            }
        return out;
    }

    void replace_broken(SimWorld& world)
    {
        for (const auto [e, item] : world.registry.view<PathItem>().each())
            item.broken = false;
    }

    void pull_down(SimWorld& world, entt::entity attraction)
    {
        entt::registry& r = world.registry;
        if (!r.valid(attraction) || !r.all_of<Attraction>(attraction))
            return;
        const bool ride = r.all_of<CycleRide>(attraction);
        for (const auto [guest, errand, stride, pose] : r.view<Errand, Stride, Pose>().each())
        {
            if (errand.target != attraction)
                continue;
            const ErrandKind was = errand.kind;
            const NodeId entrance = ride ? entrance_of(r, attraction, errand.station) : r.get<Attraction>(attraction).entrance;
            const NodeId exit = ride ? exit_of(r, attraction, errand.off) : entrance;
            // A party goes on together behind its leader, who picks somewhere else to go.
            const auto* member = r.try_get<Member>(guest);
            const bool follows = member != nullptr && r.valid(member->party) && r.get<Party>(member->party).leader != guest;
            errand = follows ? Errand { ErrandKind::Following, entt::null, 0 } : Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
            r.remove<RideWalk, Swimmer, PoolRoute>(guest);
            // Out of the queue at its entrance, riders off at its exit, swimmers out at the door; those on their
            // way walk on from where they are.
            if ((was == ErrandKind::Queueing || was == ErrandKind::Swimming) && entrance != kNoNode)
                stop_at(world.paths, entrance, stride, pose);
            else if (was == ErrandKind::Riding && exit != kNoNode)
                stop_at(world.paths, exit, stride, pose);
            else
                stride.route.clear();
        }
        r.destroy(attraction);
    }

    std::int32_t old_litter(const SimWorld& world)
    {
        const auto view = world.registry.view<const Litter>();
        return static_cast<std::int32_t>(std::ranges::count_if(view, [&](entt::entity e) { return view.get<const Litter>(e).age.value >= kTuning.litter.old.value; }));
    }

    void step(SimWorld& world, std::optional<Workers&> workers)
    {
        entt::registry& r = world.registry;

        std::optional<profiling::Scope> phase; // Each phase timed in turn, for the profiler's table
        const Weather weather = weather_in(r);
        const bool night = is_night(time_of_day(r));

        // Guests: their needs, walking, following, spacing out and arriving (simulation/guests.cpp).
        step_guests(world, workers, weather, night, phase);

        // Stalls and cash machines wear and break down (simulation/stalls.cpp).
        step_stalls(world, phase);

        // Pools, swimmers, lazy rivers and slides climbed on foot (simulation/swimming.cpp).
        step_pools(world, phase);

        // Riders on their feet walk to their seats or off to the paths (simulation/ride_walks.cpp).
        step_ride_walks(world, phase);

        // Those the player let go of, or a crash threw, fall and walk back to the paths (simulation/holding.cpp).
        step_let_go(world, phase);

        // Queues, riders and trains of the rides (simulation/rides.cpp).
        step_rides(world, weather, night, phase);

        // Parties wait for and gather their members (simulation/parties.cpp).
        step_parties(world, phase);

        phase.emplace("Tidy");
        tidy(world);
        phase.reset();
        ++world.tick;
    }

} // namespace rct3::simulation
