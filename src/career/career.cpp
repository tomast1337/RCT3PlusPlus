// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/career.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>

namespace rct3::career
{

    usize first_open(i32 pack)
    {
        return pack == 0 ? 6 : 3;
    }

    bool is_open(ScenarioStatus status)
    {
        return status != ScenarioStatus::Locked;
    }

    bool design_locked(std::optional<i32> scenario, i32 level, std::span<const Career> careers)
    {
        if (!scenario)
            return false;
        i32 best = -1;
        for (const Career& c : careers)
            if (static_cast<usize>(*scenario) < c.scenarios.size())
                best = std::max(best, static_cast<i32>(c.scenarios[static_cast<usize>(*scenario)].status));
        return best < static_cast<i32>(ScenarioStatus::Apprentice) + level;
    }

    Career new_career(std::string name, i32 pack, std::span<const std::string> ids)
    {
        Career career;
        career.name = std::move(name);
        career.pack = pack;
        fit(career, ids);
        return career;
    }

    void fit(Career& career, std::span<const std::string> ids)
    {
        std::string current;
        if (career.current < career.scenarios.size())
            current = career.scenarios[career.current].id;
        std::vector<ScenarioProgress> known = std::move(career.scenarios);
        std::ranges::move(career.unlisted, std::back_inserter(known));
        career.unlisted.clear();
        career.scenarios.clear();
        career.current = 0;
        for (const std::string& id : ids)
        {
            const auto it = std::ranges::find(known, id, &ScenarioProgress::id);
            if (it != known.end())
            {
                career.scenarios.push_back(std::move(*it));
                known.erase(it);
            }
            else
                career.scenarios.push_back({ .id = id });
            if (id == current)
                career.current = career.scenarios.size() - 1;
        }
        career.unlisted = std::move(known);
        open_earned(career);
    }

    CareerScore score(const Career& career)
    {
        CareerScore s;
        for (const ScenarioProgress& p : career.scenarios)
        {
            s.completed += p.status >= ScenarioStatus::Apprentice ? 1 : 0;
            s.entrepreneur += p.status >= ScenarioStatus::Entrepreneur ? 1 : 0;
            s.tycoon += p.status >= ScenarioStatus::Tycoon ? 1 : 0;
        }
        if (!career.scenarios.empty())
        {
            // One point a level: completing at all, then each level above apprentice.
            const usize points = s.completed + s.entrepreneur + s.tycoon;
            s.percent = static_cast<f32>(points) * 100 / static_cast<f32>(3 * career.scenarios.size());
        }
        return s;
    }

    void open_earned(Career& career)
    {
        const usize n = career.scenarios.size();
        if (n == 0)
            return;
        const CareerScore s = score(career);
        // The last two scenarios wait for the rest; every completed scenario opens one more before them.
        const usize held_back = n >= 2 ? n - 2 : 0;
        usize open = std::min(s.completed + first_open(career.pack), held_back);
        if (s.entrepreneur + 2 >= n)
            open = n - 1;
        if (s.tycoon + 1 >= n)
            open = n;
        for (usize i = 0; i < open; ++i)
        {
            if (career.scenarios[i].status == ScenarioStatus::Locked)
                career.scenarios[i].status = ScenarioStatus::Open;
        }
    }

    Lock lock(const Career& career, usize index)
    {
        const usize n = career.scenarios.size();
        if (index >= n || is_open(career.scenarios[index].status))
            return { };
        if (index + 1 == n)
            return { Lock::Kind::Tycoon };
        if (index + 2 == n)
            return { Lock::Kind::Entrepreneur };
        const auto open = static_cast<usize>(std::ranges::count_if(career.scenarios, [](const ScenarioProgress& p) { return is_open(p.status); }));
        return { Lock::Kind::Levels, index + 1 > open ? index + 1 - open : 1 };
    }

    void complete(Career& career, usize index, ScenarioStatus status, i64 now)
    {
        if (index >= career.scenarios.size())
            return;
        ScenarioProgress& p = career.scenarios[index];
        for (i32 level = 0; level < 3; ++level)
        {
            const auto reached = static_cast<ScenarioStatus>(static_cast<i32>(ScenarioStatus::Apprentice) + level);
            if (status >= reached && p.status < reached)
                p.reached[static_cast<usize>(level)] = now;
        }
        p.status = std::max(p.status, status);
        open_earned(career);
    }

} // namespace rct3::career
