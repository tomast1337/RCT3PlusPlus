// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crowds.hpp"

#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace rct3::simulation
{

    float show_chance(const Staff& staff)
    {
        const auto& t = kTuning.shows;
        const float keen = t.keen.value + t.keen_per_training.value * static_cast<float>(staff.training) + (staff.happiness.value - 0.5f) / 2 - staff.laziness.value / 2;
        return std::clamp(keen, 0.05f, 1.0f);
    }

    bool poor_show(const Staff& staff)
    {
        return staff.happiness.value < kTuning.staff.miserable.value || staff.laziness.value >= kTuning.shows.poor_lazy.value;
    }

    std::int32_t show_quality(const Staff& staff)
    {
        const std::int32_t less = (staff.happiness.value < kTuning.staff.miserable.value ? 1 : 0) + (staff.laziness.value >= kTuning.staff.tired.value ? 1 : 0);
        return std::clamp(staff.training - less, 0, staff::kMostTraining);
    }

    std::vector<ShowMove> show_moves(std::int32_t quality, bool poor, random::RandomState& dice)
    {
        using enum ShowMove;
        quality = std::clamp(quality, 0, staff::kMostTraining);
        const auto count = static_cast<std::size_t>(kTuning.shows.moves + quality);
        std::vector<ShowMove> moves;
        if (poor)
        {
            constexpr std::array kTired { Idle, IdleTwo, Wave };
            const std::size_t fall = random::below(dice, count - 1);
            for (std::size_t i = 0; moves.size() < count; ++i)
            {
                if (i == fall)
                {
                    moves.push_back(FallOver);
                    moves.push_back(GetUp);
                }
                else
                    moves.push_back(kTired[random::below(dice, kTired.size())]);
            }
            moves.resize(count);
            moves.push_back(Dejected);
            return moves;
        }
        // Better shows have more moves on offer, the fancier last.
        constexpr std::array kOffered { Wave, Idle, DanceOne, IdleTwo, DanceTwo, DanceThree };
        constexpr std::array kHeight { Wave, DanceOne, DanceTwo, DanceThree };
        const std::size_t height = random::below(dice, count);
        for (std::size_t i = 0; i < count; ++i)
            moves.push_back(i == height ? kHeight[static_cast<std::size_t>(quality)] : kOffered[random::below(dice, static_cast<std::size_t>(quality) + 3)]);
        return moves;
    }

    float show_seconds(std::span<const ShowMove> moves)
    {
        float seconds = 0;
        for (const ShowMove m : moves)
            seconds += kTuning.shows.move_length[std::to_underlying(m)].value;
        return seconds;
    }

    std::pair<ShowMove, float> show_move_at(const Show& show)
    {
        float into = show.into.value;
        for (const ShowMove m : show.moves)
        {
            const float length = kTuning.shows.move_length[std::to_underlying(m)].value;
            if (into < length || &m == &show.moves.back())
                return { m, std::min(into, length) };
            into -= length;
        }
        return { ShowMove::Idle, 0.0f };
    }

    void step_crowds(SimWorld& world)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;
        if (paths.nodes().empty())
            return;
        const auto staff_view = r.view<Staff, Stride, Pose, Dice>();

        // Crowds: guests stopped round an entertainer's show or a VIP (see crowds.hpp).
        const auto flat = [](glm::vec3 v) { return glm::vec2 { v.x, v.z }; };
        // Guests walking free within `reach` of `star` (alone, or leading their party), each with their party.
        const auto free_near = [&](entt::entity star, float reach) {
            std::vector<entt::entity> found;
            const glm::vec2 at = flat(r.get<Pose>(star).position);
            for (const auto [g, visitor, errand, stride, pose] : r.view<const Visitor, const Errand, const Stride, const Pose>(entt::exclude<Staff, Vip, Watching, Linger, Gesture, Left, Held, Dropped, RideWalk, Crossing>).each())
            {
                const float d = glm::distance(flat(pose.position), at);
                if (errand.kind != ErrandKind::Wandering || stride.from >= paths.nodes().size() || d > reach || d < kTuning.shows.near.value)
                    continue;
                if (const auto* member = r.try_get<Member>(g); member != nullptr && (r.get<Party>(member->party).leader != g || r.get<Party>(member->party).huddle > 0))
                    continue;
                found.push_back(g);
            }
            std::ranges::sort(found);
            return found;
        };
        // `guest` and their party stop to watch `star`, each on their own side of them. A fan goes alone, the
        // party following them.
        const auto draw_in = [&](entt::entity star, entt::entity guest, bool autograph) {
            const auto* member = r.try_get<Member>(guest);
            const std::vector<entt::entity> who = member != nullptr && !autograph ? r.get<Party>(member->party).members : std::vector { guest };
            const glm::vec2 off = flat(r.get<Pose>(guest).position) - flat(r.get<Pose>(star).position);
            const float angle = std::atan2(off.x, off.y);
            for (std::size_t k = 0; k < who.size(); ++k)
                if (r.all_of<Stride, Pose, Mood>(who[k]) && !r.any_of<Gesture, Linger, Watching>(who[k]))
                    r.emplace<Watching>(who[k], Watching { .star = star, .angle = { angle + 0.4f * (static_cast<float>(k) - static_cast<float>(who.size() - 1) / 2) }, .autograph = autograph });
        };
        // Done watching `star`: the crowd shows what they made of it, a good show or a poor one.
        const auto show_over = [&](entt::entity star, std::optional<bool> poor) {
            for (const auto [g, watching] : r.view<const Watching>().each())
            {
                if (watching.star != star || watching.autograph)
                    continue;
                if (poor && r.all_of<Mood, Dice>(g))
                {
                    auto& mood = r.get<Mood>(g);
                    auto& dice = r.get<Dice>(g).state;
                    if (*poor)
                    {
                        // Children cry or take fright; the rest shake their heads, look at their watches or fold their arms.
                        constexpr std::array kUpset { GestureKind::Cry, GestureKind::Horror };
                        constexpr std::array kBored { GestureKind::Grumble, GestureKind::LookWatch, GestureKind::Disapproval };
                        const bool child = r.all_of<Visitor>(g) && r.get<Visitor>(g).age == AgeGroup::Child;
                        r.emplace_or_replace<Gesture>(g, child ? kUpset[random::below(dice, kUpset.size())] : kBored[random::below(dice, kBored.size())], kTuning.gestures.shown.value);
                    }
                    else
                    {
                        constexpr std::array kGlad { GestureKind::Clap, GestureKind::Clap, GestureKind::Cheer, GestureKind::Laugh };
                        const GestureKind kind = kGlad[random::below(dice, kGlad.size())];
                        r.emplace_or_replace<Gesture>(g, kind, kind == GestureKind::Clap ? kTuning.shows.clap.value : kTuning.gestures.shown.value);
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.shows.cheer.value);
                    }
                    think(r, g, *poor ? ThoughtKind::BoringEntertainer : ThoughtKind::GreatEntertainer, star, now);
                }
            }
            const auto view = r.view<const Watching>();
            for (const entt::entity g : sorted(view))
                if (view.get<const Watching>(g).star == star)
                    r.remove<Watching>(g);
        };

        // Entertainers: a show under way goes on, drawing guests in, till its moves are through or they are
        // picked up.
        for (const entt::entity e : sorted(r.view<Staff, Show, Dice>()))
        {
            Show& show = r.get<Show>(e);
            Staff& staff = r.get<Staff>(e);
            show.gap.value -= dt;
            if (show.moves.empty())
                continue;
            show.into.value += dt;
            const bool stopped = r.any_of<Held, Dropped>(e) || !r.all_of<Stride>(e);
            if (!stopped && show.into.value < show_seconds(show.moves))
            {
                staff.busy.value = std::max(staff.busy.value, dt); // Stands still for it
                for (const entt::entity g : free_near(e, kTuning.shows.reach.value))
                {
                    const auto* member = r.try_get<Member>(g);
                    const auto many = member != nullptr ? r.get<Party>(member->party).members.size() : 1;
                    if (random::chance(r.get<Dice>(e).state, kTuning.shows.join.value * dt / static_cast<double>(many)))
                        draw_in(e, g, false);
                }
                continue;
            }
            if (show.poor && !stopped)
                staff.happiness.value = clamp01(staff.happiness.value - kTuning.shows.poor_gloom.value);
            show_over(e, stopped ? std::nullopt : std::optional { show.poor });
            show.moves.clear();
            show.gap = kTuning.shows.gap;
        }
        // A free one, now and then, puts on a show when they find guests near and feel like it.
        for (const entt::entity e : sorted(staff_view))
        {
            auto [staff, stride, pose, dice] = staff_view.get(e);
            if (staff.role != StaffRole::Entertainer)
                continue;
            Show& show = r.get_or_emplace<Show>(e);
            if (!show.moves.empty() || show.gap.value > 0 || staff.busy.value > 0 || r.any_of<Held, Dropped>(e) || stride.from >= paths.nodes().size())
                continue;
            show.gap = kTuning.shows.look;
            if (free_near(e, kTuning.shows.reach.value).empty() || !random::chance(dice.state, show_chance(staff)))
                continue;
            show.poor = poor_show(staff);
            show.moves = show_moves(show_quality(staff), show.poor, dice.state);
            show.into = { };
            staff.busy.value = dt;
        }

        // VIPs: now and then fans gather round them, and they sign for those near enough, one at a time.
        for (const entt::entity v : sorted(r.view<Vip, const Pose, Dice>(entt::exclude<Left>)))
        {
            Vip& vip = r.get<Vip>(v);
            if (vip.leaving)
                continue;
            auto& dice = r.get<Dice>(v).state;
            if (vip.poll.value -= dt; vip.poll.value <= 0)
            {
                vip.poll = kTuning.fans.poll;
                for (const entt::entity g : free_near(v, kTuning.shows.reach.value))
                {
                    const auto* member = r.try_get<Member>(g);
                    const auto many = member != nullptr ? r.get<Party>(member->party).members.size() : 1;
                    const auto* got = r.try_get<Autographs>(g);
                    if ((got == nullptr || !std::ranges::contains(got->from, v)) && random::chance(dice, kTuning.fans.keen.value / static_cast<double>(many)))
                        draw_in(v, g, true);
                }
            }
            if (r.all_of<Gesture>(v) || r.any_of<Held, Dropped>(v))
                continue;
            const glm::vec2 at = flat(r.get<Pose>(v).position);
            const auto view = r.view<const Watching, const Pose>();
            for (const entt::entity g : sorted(view))
            {
                const auto& watching = view.get<const Watching>(g);
                if (watching.star != v || !watching.autograph || glm::distance(flat(view.get<const Pose>(g).position), at) > kTuning.fans.arm.value)
                    continue;
                r.emplace_or_replace<Gesture>(v, GestureKind::HandOver, kTuning.gestures.shown);
                r.remove<Watching>(g);
                r.emplace_or_replace<Gesture>(g, GestureKind::HandOver, kTuning.gestures.shown);
                r.get_or_emplace<Autographs>(g).from.push_back(v);
                auto& mood = r.get<Mood>(g);
                mood.happiness.value = clamp01(mood.happiness.value + kTuning.fans.cheer.value);
                ++vip.signatures;
                break;
            }
        }

        // Those watching step to their place round the star and face them; a fan gives up after a while.
        for (auto [v, vip] : r.view<Vip>().each())
            vip.fans = 0;
        const auto watchers = r.view<Watching, Stride, Pose>();
        const auto listed = sorted(watchers);
        for (const entt::entity g : listed)
        {
            auto [watching, stride, pose] = watchers.get(g);
            watching.waited.value += dt;
            const bool gone = !r.valid(watching.star) || r.any_of<Left>(watching.star) || !r.all_of<Pose>(watching.star);
            const auto* vip = gone ? nullptr : r.try_get<Vip>(watching.star);
            const float apart = gone ? 0.0f : glm::distance(flat(pose.position), flat(r.get<Pose>(watching.star).position));
            const bool tired = watching.waited.value >= kTuning.fans.give_up.value || apart >= kTuning.fans.far.value;
            if (gone || (watching.autograph && (vip == nullptr || vip->leaving || tired)) || (!watching.autograph && r.get_or_emplace<Show>(watching.star).moves.empty()) || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
            {
                r.remove<Watching>(g);
                continue;
            }
            // A fan too far off walks after the VIP over the paths, to the tile they are on now; the walk is
            // planned again whenever the VIP moves to another tile. No way there: they give up.
            watching.chasing = watching.autograph && apart > kTuning.fans.close.value;
            if (watching.chasing)
            {
                const Stride& at = r.get<Stride>(watching.star);
                const NodeId target = at.progress.value < 0.5f ? at.from : at.to;
                if (target == stride.to)
                    stride.route.clear();
                else if (stride.route.empty() || stride.route.back() != target)
                {
                    auto way = paths.route(stride.to, target, Walker::Guest, r.get<Wallet>(g).cash.value);
                    if (!way)
                    {
                        r.remove<Watching>(g);
                        continue;
                    }
                    stride.route = std::move(*way);
                }
                continue;
            }
            if (watching.autograph)
            {
                stride.route.clear();
                ++r.get<Vip>(watching.star).fans;
            }
            const glm::vec2 star = flat(r.get<Pose>(watching.star).position);
            const float ring = watching.autograph ? kTuning.fans.ring.value : kTuning.shows.ring.value;
            const glm::vec2 want = star + ring * glm::vec2 { std::sin(watching.angle.value), std::cos(watching.angle.value) };
            // The spot kept on the path: on whichever of their own tiles or the star's comes nearest to it.
            glm::vec2 spot = want;
            float best = std::numeric_limits<float>::infinity();
            const auto* star_stride = r.try_get<Stride>(watching.star);
            for (const NodeId n : { stride.from, stride.to, star_stride != nullptr ? star_stride->from : kNoNode, star_stride != nullptr ? star_stride->to : kNoNode })
                if (n < paths.nodes().size())
                {
                    const glm::vec2 centre = flat(paths.centre(n));
                    const glm::vec2 kept = centre + glm::clamp(want - centre, -kLaneReach, kLaneReach);
                    if (const float d = glm::distance(kept, want); d < best)
                    {
                        best = d;
                        spot = kept;
                    }
                }
            // Both ends of their step shifted alike, so they stand on the spot and walk on from there after,
            // drifting back across the path over their next step.
            const glm::vec2 middle = glm::mix(flat(paths.centre(stride.from)), flat(paths.centre(stride.to)), stride.progress.value);
            const glm::vec2 lane = spot - middle;
            const glm::vec2 was = glm::mix(stride.lane_from, stride.lane_to, stride.progress.value);
            const glm::vec2 step = lane - was;
            const float most = kTuning.walking.lane_drift.value * 4 * dt;
            const glm::vec2 now_lane = was + (glm::length(step) > most ? step * (most / glm::length(step)) : step);
            stride.lane_from = stride.lane_to = now_lane;
            place(paths, stride, pose);
            const glm::vec2 look = star - flat(pose.position);
            if (glm::dot(look, look) > 0.0001f)
                pose.heading.value = std::atan2(look.x, look.y);
        }
    }

} // namespace rct3::simulation
