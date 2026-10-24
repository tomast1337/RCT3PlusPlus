// SPDX-License-Identifier: GPL-3.0-or-later
//
// A career: the player's way through one game's scenarios, the original's or an expansion's. Each scenario
// is locked, open, or completed at one of the three levels of its objectives (apprentice, entrepreneur,
// tycoon). A career starts with the first few scenarios open, six in the original game and three in an
// expansion, and every scenario completed at any level opens one more. The last two are held back: the one
// before last opens once every other scenario but the last two is done at entrepreneur or better, and the
// last once every other is done at tycoon. Nothing that is open is ever locked again.
//
// Scenarios are known by their id, as the game's list names them, not by their place in the list: when the
// list changes, fit() puts each scenario's progress back by id, and keeps the progress of scenarios no
// longer listed apart so it is not lost.
//
// A career is scored by its levels: one point for each scenario done at apprentice, two at entrepreneur and
// three at tycoon, out of three for each scenario.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rct3::career
{

    enum class ScenarioStatus : std::uint8_t
    {
        Locked,
        Open,
        Apprentice,
        Entrepreneur,
        Tycoon,
    };

    struct ScenarioProgress
    {
        std::string id; ///< As the game's list names the scenario
        ScenarioStatus status = ScenarioStatus::Locked;
        std::string last_save { }; ///< The game last saved in this scenario, in the career's folder; empty for none
        /// When each level was first reached: apprentice, entrepreneur, tycoon; seconds since 1970, 0 not yet.
        std::array<std::int64_t, 3> reached { };
        std::uint64_t play_seconds = 0; ///< Time played in this scenario
    };

    struct Career
    {
        std::string name;
        int pack = 0;                            ///< 0 the original game, then each expansion
        std::size_t current = 0;                 ///< The scenario last picked, in `scenarios`
        std::int64_t created = 0;                ///< Seconds since 1970; 0 when not known
        std::vector<ScenarioProgress> scenarios; ///< In the order of the game's list
        std::vector<ScenarioProgress> unlisted;  ///< Of scenarios the game's list no longer has, kept as they were
    };

    struct CareerScore
    {
        std::size_t completed = 0;    ///< At any level
        std::size_t entrepreneur = 0; ///< At entrepreneur or better
        std::size_t tycoon = 0;
        float percent = 0; ///< Of the most there is
    };

    /// How many scenarios a new career has open.
    [[nodiscard]] std::size_t first_open(int pack);

    /// A new career through the scenarios `ids`, in the list's order, the first ones open.
    [[nodiscard]] Career new_career(std::string name, int pack, std::span<const std::string> ids);

    /// Fits the career to the game's list of scenarios `ids`: each keeps its progress by id, new ones come
    /// locked, ones no longer listed are kept apart, the scenario last picked stays picked if still listed,
    /// and those the rule opens are opened.
    void fit(Career& career, std::span<const std::string> ids);

    /// Opens the scenarios the career has earned.
    void open_earned(Career& career);

    /// Records scenario `index` done at `status`, if that is better than before, at `now` (seconds since
    /// 1970), and opens what it earns.
    void complete(Career& career, std::size_t index, ScenarioStatus status, std::int64_t now);

    [[nodiscard]] CareerScore score(const Career& career);

    /// What a locked scenario waits for.
    struct Lock
    {
        enum class Kind : std::uint8_t
        {
            Open,         ///< Not locked
            Levels,       ///< `levels` more scenarios completed at any level
            Entrepreneur, ///< Entrepreneur or better on all the others but the last
            Tycoon,       ///< Tycoon on all the others
        };
        Kind kind = Kind::Open;
        std::size_t levels = 0;
    };
    [[nodiscard]] Lock lock(const Career& career, std::size_t index);

    [[nodiscard]] bool is_open(ScenarioStatus status);

    /// Whether a saved design that asks for scenario `scenario` (by place in the list; -1 for none) to be
    /// completed at `level` (0 apprentice, 1 entrepreneur, 2 tycoon) is still locked, by the best any of
    /// `careers` has done there. With no career it is locked.
    [[nodiscard]] bool design_locked(std::int32_t scenario, std::int32_t level, std::span<const Career> careers);

} // namespace rct3::career
