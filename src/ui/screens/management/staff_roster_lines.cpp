// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management/staff_roster_lines.hpp"

#include "simulation/staff/care.hpp"
#include "ui/save_text.hpp"
#include "ui/screens/info/person.hpp"

#include <algorithm>
#include <bit>
#include <tuple>

namespace rct3::ui::screens::management
{

    namespace
    {
        /// The place of `kind` among the switches; the jobs without one after them.
        std::size_t job_rank(save::model::StaffKind kind)
        {
            return static_cast<std::size_t>(std::ranges::find(kRosterJobs, kind, &RosterJob::kind) - kRosterJobs.begin());
        }
    } // namespace

    bool shows_job(RosterJobs jobs, save::model::StaffKind kind)
    {
        // Lifeguards belong to their chairs, not to the list.
        if (kind == save::model::StaffKind::LifeGuard)
            return false;
        const std::size_t rank = job_rank(kind);
        return rank >= kRosterJobs.size() || (jobs & (1u << rank)) != 0;
    }

    std::vector<RosterLine> roster_lines(const ManagementFacts& facts, const StringTable& strings, RosterJobs jobs, RosterSort sort)
    {
        std::vector<RosterLine> lines;
        if (facts.people == nullptr)
            return lines;
        for (std::size_t i = 0; i < facts.people->staff.size(); ++i)
        {
            const save::model::Staff& s = facts.people->staff[i];
            if (!shows_job(jobs, s.kind))
                continue;
            const info::PersonFacts person { .staff = &s, .people = facts.people, .rides = facts.rides };
            simulation::Staff pay { .role = simulation::staff::staff_role(s.kind), .training = s.training };
            simulation::staff::set_wage_level(pay, s.wage_level);
            const StaffLook look = i < facts.staff_looks.size() ? facts.staff_looks[i] : StaffLook { };
            // Staff a scenario starts with were hired before its calendar began: they show its first day.
            const save::model::CalendarDate hired = save::model::GameClock::date_at(s.person.start_time, facts.zero_time);
            lines.push_back(
                {
                    .index = i,
                    .kind = s.kind,
                    .name = info::person_name(person, strings),
                    .doing = info::person_doing(person, strings),
                    .mood = happiness_mood(s.happiness),
                    .happiness = s.happiness,
                    .laziness = s.laziness,
                    .warnings = s.warnings,
                    .training = s.training,
                    .wage_level = pay.wage_level,
                    .wage = pay.wage.value,
                    .training_cost = simulation::staff::training_cost(pay),
                    .tasks = s.task_flags,
                    .patrol_tiles = look.patrol_tiles,
                    .painting_patrol = look.painting_patrol,
                    .colours = look.colours,
                    .costume = s.person.sub_type,
                    .hired = s.person.start_time,
                    .hired_text = date_text(strings, hired.day, hired.month, static_cast<std::int32_t>(hired.year + 1)),
                });
        }
        // Each sort's key, lowest first; keys where the highest come first are turned round.
        const auto key = [&](const RosterLine& l) -> std::tuple<double, std::u16string_view> {
            switch (sort)
            {
                case RosterSort::Job:
                case RosterSort::Count_:
                    return { 0, { } };
                case RosterSort::Name:
                    return { 0, l.name };
                case RosterSort::PatrolArea:
                    return { -static_cast<double>(l.patrol_tiles), { } };
                case RosterSort::Tasks:
                    return { -static_cast<double>(std::popcount(static_cast<std::uint32_t>(l.tasks))), { } };
                case RosterSort::Uniform:
                    return { l.colours[0] * 256.0 + l.colours[1], { } };
                case RosterSort::Wage:
                    return { -static_cast<double>(l.wage), { } };
                case RosterSort::Activity:
                    return { 0, l.doing };
                case RosterSort::Happiness:
                    return { -l.happiness, { } };
                case RosterSort::Warnings:
                    return { -static_cast<double>(l.warnings), { } };
                case RosterSort::Training:
                    return { -static_cast<double>(l.training), { } };
                case RosterSort::StartDate:
                    return { l.hired, { } };
                case RosterSort::Laziness:
                    return { -l.laziness, { } };
            }
            return { 0, { } };
        };
        std::ranges::stable_sort(lines, [&](const RosterLine& a, const RosterLine& b) { return std::tuple_cat(key(a), std::tuple(job_rank(a.kind))) < std::tuple_cat(key(b), std::tuple(job_rank(b.kind))); });
        return lines;
    }

} // namespace rct3::ui::screens::management
