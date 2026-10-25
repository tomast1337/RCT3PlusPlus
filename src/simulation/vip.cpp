// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/vip.hpp"

#include "simulation/components.hpp"
#include "simulation/objectives.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <ranges>

namespace rct3::simulation
{
    namespace
    {
        constexpr float kMonth = save::GameClock::kUnitsPerMonth;
        constexpr float kWeek = save::GameClock::kUnitsPerWeek;

        /// Whether `ride` is broken down, or has a train lying wrecked.
        bool out_of_order(const entt::registry& r, entt::entity ride)
        {
            if (const auto* upkeep = r.try_get<Upkeep>(ride); upkeep != nullptr && upkeep->broken)
                return true;
            const auto* wrecks = r.try_get<Wrecks>(ride);
            return wrecks != nullptr && std::ranges::any_of(wrecks->trains, [](const auto& t) { return !t.empty(); });
        }
    } // namespace

    std::string_view vip_name_key(std::uint8_t who) noexcept
    {
        // The name each celebrity's own data file gives them, by the number the goals use.
        constexpr std::array<std::string_view, 7> kNames { "", "PoliticianName", "PopStarName", "MovieStarName", "SportsStar1Name", "BobWatermanName", "SmytheName" };
        return who < kNames.size() ? kNames[who] : std::string_view { };
    }

    float vip_opinion(const Vip& vip, const save::VipTarget& target, float happiness)
    {
        using T = save::VipTarget;
        std::int32_t wanted = 0;
        std::int32_t seen = 0;
        const auto tick = [&](std::uint32_t wish, std::int32_t needed, std::int32_t done) {
            if ((target.wishes & wish) == 0)
                return;
            wanted += needed;
            seen += std::min(done, needed);
        };
        tick(T::kCoasters, std::max(target.coasters_needed, 0), static_cast<std::int32_t>(vip.coasters.size()));
        tick(T::kFireworks, 1, vip.fireworks ? 1 : 0);
        tick(T::kThemedArea, 1, vip.themed ? 1 : 0);
        // shortcut: pools and viewing galleries are not simulated, so a VIP wanting one counts it seen; judge them
        // once rct3++ has swimming pools and animal galleries.
        tick(T::kPool, 1, 1);
        tick(T::kGalleries, 1, 1);
        return wanted > 0 ? static_cast<float>(seen) / static_cast<float>(wanted) : happiness;
    }

    void advance_vips(SimWorld& world, save::ObjectiveSet& set, float time, float game_seconds, ParkMessages& messages, const VipSurroundings& around)
    {
        using T = save::VipTarget;
        entt::registry& r = world.registry;
        const tuning::Vips& tune = kTuning.vips;
        const std::int32_t level = level_playing(set);
        const auto name_of = [&](std::uint8_t who) { return around.name ? around.name(who) : std::string { }; };
        const auto tell = [&](std::string_view key, std::uint8_t who) { post(messages, { .text_key = std::string(key), .subject_name = name_of(who) }); };
        // shortcut: VIP messages name the VIP but have no camera on them, as guests who came in since the park
        // was loaded have no id a message can keep; point them at the VIP once such guests are given one.
        for (save::Condition& c : set.conditions)
        {
            if (!c.vip || c.objective_id != 0)
                continue;
            T& goal = *c.vip;
            // A VIP who has gone home: the visit is judged.
            const auto here = r.view<Vip>();
            const auto mine = std::ranges::find_if(here, [&](entt::entity e) { return here.get<Vip>(e).goal == c.id; });
            if (mine != here.end() && r.all_of<Left>(*mine))
            {
                const Vip& vip = r.get<Vip>(*mine);
                const float average = vip.looks > 0 ? vip.happiness_sum / static_cast<float>(vip.looks) : 0.0f;
                const bool pleased = vip.patience.value > 0 && ((goal.wishes & T::kHappiness) == 0 || average >= goal.target_happiness);
                const bool wishes = (goal.wishes & (T::kCoasters | T::kFireworks | T::kThemedArea | T::kPool | T::kGalleries)) != 0;
                const bool well = pleased && (!wishes || vip_opinion(vip, goal, average) >= 1.0f);
                tell("VIP_LeftPark", vip.who);
                tell(well ? "VIPCondition_Success" : "VIPCondition_Failure", vip.who);
                if (well)
                    goal.met = true;
                else
                    goal.next_visit = time + static_cast<float>(goal.retry_months) * kMonth;
                r.remove<Vip>(*mine);
                continue;
            }
            if (goal.met)
                continue;
            if (c.difficulty != level)
            {
                // Not this level's goal yet: its date keeps a little ahead of the calendar.
                while (goal.next_visit < time)
                    goal.next_visit += kMonth;
                continue;
            }
            if (mine == here.end())
            {
                if (!goal.warned_week && time >= goal.next_visit - kWeek)
                {
                    goal.warned_week = goal.warned_month = true;
                    tell("VIPCondition_WeekNotice", goal.who);
                }
                else if (!goal.warned_month && time >= goal.next_visit - kMonth)
                {
                    goal.warned_month = true;
                    tell("VIPCondition_MonthNotice", goal.who);
                }
                if (time < goal.next_visit)
                    continue;
                const auto gates = r.view<const ParkGate, Dice>();
                if (gates.begin() == gates.end())
                    continue; // The park has no gate yet; they come once it does
                const entt::entity gate = *gates.begin();
                // Celebrities 2 and 3 are women, as their models are.
                const std::array<std::pair<AgeGroup, bool>, 1> who { std::pair { AgeGroup::Adult, goal.who == 2 || goal.who == 3 } };
                const entt::entity guest = admit_guests(world, gates.get<Dice>(gate).state, gates.get<const ParkGate>(gate).node, who, kTuning.parties.middle).front();
                r.get<Visitor>(guest).name = name_of(goal.who);
                r.emplace<Vip>(guest, Vip { .goal = c.id, .who = goal.who, .came = time, .look = tune.look });
                goal.warned_week = goal.warned_month = false;
                tell("VIP_ArrivedInPark", goal.who);
                continue;
            }
            // In the park: they look round every so often.
            const entt::entity guest = *mine;
            Vip& vip = r.get<Vip>(guest);
            vip.look.value -= game_seconds;
            if (vip.look.value > 0 || vip.leaving)
                continue;
            vip.look = tune.look;
            const float happiness = r.all_of<Mood>(guest) ? r.get<Mood>(guest).happiness.value : 0.0f;
            vip.happiness_sum += happiness;
            ++vip.looks;
            if (vip.patience.value > 0)
                vip.patience.value = std::min(vip.patience.value + happiness * tune.cheer.value, 1.0f);
            const auto lose = [&](float tolerance, float share) {
                if (tolerance > 0)
                    vip.patience.value = std::max(vip.patience.value - share / tolerance, 0.0f);
            };
            if ((goal.wishes & T::kLitter) != 0)
                if (const auto* stride = r.try_get<Stride>(guest))
                {
                    const auto near = std::ranges::count_if(r.view<const Litter>().each(), [&](const auto& l) { return std::get<1>(l).node == stride->from || std::get<1>(l).node == stride->to; });
                    if (near > 0)
                        lose(goal.litter_tolerance, near >= tune.heap ? 1.0f : tune.few.value);
                }
            if (const auto* errand = r.try_get<Errand>(guest); errand != nullptr && errand->kind == ErrandKind::Riding && r.valid(errand->target))
            {
                const entt::entity ride = errand->target;
                if ((goal.wishes & T::kBreakdowns) != 0 && out_of_order(r, ride))
                    lose(goal.breakdown_tolerance, 1.0f);
                // shortcut: any ride with trains counts as a coaster, as the simulation does not tell coasters from
                // other tracked rides; tell them apart once it keeps a ride's kind.
                if (const auto* cycle = r.try_get<CycleRide>(ride); cycle != nullptr && r.all_of<Trains>(ride) && cycle->excitement.value * 10.0f >= goal.min_excitement && !std::ranges::contains(vip.coasters, ride))
                    vip.coasters.push_back(ride);
            }
            // shortcut: rct3++ has no firework shows yet, so a VIP who wants fireworks is pleased by being in the
            // park after dark, when shows go up; judge them by a show they watch once shows are put on.
            const auto* daylight = r.ctx().find<Daylight>();
            if (is_night(daylight != nullptr ? daylight->time.value : Daylight { }.time.value))
                vip.fireworks = true;
            if ((goal.wishes & T::kThemedArea) != 0 && around.themed && r.all_of<Pose>(guest) && around.themed(r.get<Pose>(guest).position, goal.theme, tune.reach) >= goal.scenery_needed)
                vip.themed = true;
            // shortcut: autograph hunters, dung and animals are not simulated, so those tolerances never wear a VIP
            // down; add them with crowds that gather round VIPs and with animals.
            if (vip.patience.value <= 0 || time - vip.came >= static_cast<float>(goal.months_to_stay) * kMonth)
            {
                vip.leaving = true;
                tell("VIP_IsLeavingPark", vip.who);
            }
        }
    }

} // namespace rct3::simulation
