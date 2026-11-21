// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/boat.hpp"

#include "core/types.hpp"
#include "math/portable.hpp"
#include "park/water/fill.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>

namespace rct3::simulation
{

    namespace
    {
        // Picked to look right on a park lake.
        constexpr units::Seconds kStep { 0.1f };       ///< A part step
        constexpr units::Distance kReached { 2.5f };   ///< This near a mark counts as there
        constexpr units::Seconds kGiveUp { 12 };       ///< On one mark this long, it picks another
        constexpr units::Pace kSlowest { 1 };          ///< It keeps to this, however hard it turns
        constexpr i32 kReach = 6;                      ///< Tiles away it may pick its next mark
        constexpr i32 kTries = 20;                     ///< Picks before it settles for any water
        constexpr units::Distance kSlowNear { 6 };     ///< This near its last mark it starts to brake
        constexpr units::Radians kAhead { 0.5f };      ///< Its mark may lie this far off its bow for it to speed up
        constexpr units::Distance kLookAhead { 4 };    ///< How far ahead it looks for water to go on in
        constexpr units::Radians kLookWide { 0.5f };   ///< And this far either side of its bow
        constexpr units::Distance kBeam { 2 };         ///< How far to its side it looks for water to turn round in
        constexpr f32 kTurnedRound = 0.75f;            ///< Of a half turn, its mark this far off its bow has it turn round
        constexpr units::Seconds kLeanSettle { 0.5f }; ///< For its lean to follow its turn
        constexpr units::Distance kHull { 2 };         ///< Two boats' middles this close touch
        constexpr units::Distance kKeepClear { 10 };   ///< Another boat this far ahead, it starts to steer round it
        constexpr units::Radians kDodge { 2 };         ///< The most it steers off its mark to keep clear
        constexpr f32 kBounce = 0.6f;                  ///< Of its speed a bumping boat keeps through a bump

        f32 angle_between(glm::vec2 from, glm::vec2 to)
        {
            return math::atan2(from.x * to.y - from.y * to.x, glm::dot(from, to));
        }

        glm::vec2 turned(glm::vec2 v, f32 angle)
        {
            const f32 c = math::cos(angle), s = math::sin(angle);
            return { v.x * c - v.y * s, v.x * s + v.y * c };
        }

        /// Marks along the route to `goal`, `goal` last; empty when the water does not reach it.
        std::vector<glm::vec2> marks_to(const WaterTiles& water, glm::vec2 from, glm::vec2 goal)
        {
            const auto way = water_route(water, water.tile_of(from), water.tile_of(goal));
            if (!way)
                return { };
            std::vector<glm::vec2> marks;
            for (usize i = 0; i + 1 < way->size(); ++i)
                marks.push_back(water.middle((*way)[i]));
            marks.push_back(goal);
            return marks;
        }
    } // namespace

    std::optional<f32> WaterTiles::at(i32 col, i32 row) const
    {
        if (col < 0 || row < 0 || col >= cols || row >= rows)
            return std::nullopt;
        return level[static_cast<usize>(row) * static_cast<usize>(cols) + static_cast<usize>(col)];
    }

    glm::ivec2 WaterTiles::tile_of(glm::vec2 p) const
    {
        return { static_cast<i32>(std::floor((p.x - origin_x) / tile_size)), static_cast<i32>(std::floor((p.y - origin_z) / tile_size)) };
    }

    glm::vec2 WaterTiles::middle(glm::ivec2 tile) const
    {
        return { origin_x + (static_cast<f32>(tile.x) + 0.5f) * tile_size, origin_z + (static_cast<f32>(tile.y) + 0.5f) * tile_size };
    }

    WaterTiles water_tiles(const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> bodies, const rct3::park::path::SolidTiles& solids)
    {
        WaterTiles w { .cols = grid.width, .rows = grid.height, .origin_x = grid.origin_x, .origin_z = grid.origin_z, .tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : 4 };
        w.level.assign(static_cast<usize>(std::max(grid.width, 0)) * static_cast<usize>(std::max(grid.height, 0)), std::nullopt);
        for (const save::model::WaterBody& body : bodies)
        {
            // Both triangles with all three corners under the surface.
            std::vector<u8> under(w.level.size(), 0);
            for (const save::model::WaterTri& t : body.tris)
                if (t.col < w.cols && t.row < w.rows && t.mask == 0b111)
                    under[static_cast<usize>(t.row) * static_cast<usize>(w.cols) + t.col] |= static_cast<u8>(1u << (t.triangle & 1));
            for (usize i = 0; i < under.size(); ++i)
                if (under[i] == 0b11)
                    w.level[i] = body.level;
        }
        // Closed where rocks, scenery and the like stand in the water or hang lower over it than a boat clears;
        // what stands only along a tile's edges a boat slips past.
        constexpr units::Distance kUnder { 1 }; ///< Something may reach up from this far under the surface
        constexpr units::Distance kClear { 1 }; ///< A boat needs this much room over the surface
        for (const auto& [key, spans] : solids.tiles)
        {
            const i32 col = static_cast<i32>(key & 0xFFFF), row = static_cast<i32>(key >> 16);
            const auto level = w.at(col, row);
            if (level && std::ranges::any_of(spans, [&](const rct3::park::path::SolidSpan& s) { return (s.room & rct3::park::path::kWholeTile) != 0 && s.low < *level + kClear.value && *level - kUnder.value < s.high; }))
                w.level[static_cast<usize>(row) * static_cast<usize>(w.cols) + static_cast<usize>(col)] = std::nullopt;
        }
        return w;
    }

    std::optional<std::vector<glm::ivec2>> water_route(const WaterTiles& water, glm::ivec2 from, glm::ivec2 to)
    {
        if (!water.at(to.x, to.y) || from.x < 0 || from.y < 0 || from.x >= water.cols || from.y >= water.rows)
            return std::nullopt;
        if (from == to)
            return std::vector<glm::ivec2> { to };
        // Breadth first from the goal, so each tile knows its next step towards it.
        const auto index = [&](glm::ivec2 t) { return static_cast<usize>(t.y) * static_cast<usize>(water.cols) + static_cast<usize>(t.x); };
        std::vector<i32> next(water.level.size(), -1);
        std::deque<glm::ivec2> open { to };
        next[index(to)] = static_cast<i32>(index(to));
        constexpr std::array<glm::ivec2, 8> kSteps { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } } };
        while (!open.empty() && next[index(from)] < 0)
        {
            const glm::ivec2 t = open.front();
            open.pop_front();
            for (const glm::ivec2 step : kSteps)
            {
                const glm::ivec2 n = t + step;
                // The start may be on the dock's edge; every other tile must float a boat. Corners only past water.
                if ((n != from && !water.at(n.x, n.y)) || next[index(n)] >= 0)
                    continue;
                if (step.x != 0 && step.y != 0 && (!water.at(t.x - step.x, t.y) || !water.at(t.x, t.y - step.y)))
                    continue;
                next[index(n)] = static_cast<i32>(index(t));
                open.push_back(n);
            }
        }
        if (next[index(from)] < 0)
            return std::nullopt;
        std::vector<glm::ivec2> way;
        for (usize at = static_cast<usize>(next[index(from)]);; at = static_cast<usize>(next[at]))
        {
            way.push_back({ static_cast<i32>(at % static_cast<usize>(water.cols)), static_cast<i32>(at / static_cast<usize>(water.cols)) });
            if (at == index(to))
                break;
        }
        return way;
    }

    BoatPose between_sails(const Boat& boat, f32 along)
    {
        const f32 turn = angle_between(boat.was_heading, boat.heading);
        return { glm::mix(boat.was, boat.at, along), turned(boat.was_heading, turn * along), { glm::mix(boat.was_lean.value, boat.lean.value, along) } };
    }

    Stroke stroke(units::TurnRate turning)
    {
        return turning.value > kRowRound.value ? Stroke::Left : turning.value < -kRowRound.value ? Stroke::Right : Stroke::Both;
    }

    void cast_off(Boat& boat, const Dock& dock, bool roams, std::span<const glm::vec2> calls)
    {
        boat.at = dock.leave;
        boat.heading = glm::length(dock.heading) > 0 ? glm::normalize(dock.heading) : glm::vec2 { 0, 1 };
        boat.speed = kSlowest;
        boat.state = roams ? Sailing::Out : Sailing::Home;
        boat.marks.clear();
        boat.calls.clear();
        if (!roams)
            boat.calls.assign(calls.begin(), calls.end());
        boat.calls.push_back(dock.enter);
        boat.out = { };
        boat.on_mark = { };
        boat.turning = { };
        boat.lean = { };
        boat.was = boat.at;
        boat.was_heading = boat.heading;
        boat.was_lean = { };
    }

    bool sail(Boat& boat, const WaterTiles& water, const Dock& dock, const ovl::BoatHandling& handling, f32 top, f32 roam, f32 seconds, random::RandomState& dice, std::span<const Boat> fleet, bool bumps)
    {
        if (boat.state == Sailing::Docked)
            return false;
        boat.was = boat.at;
        boat.was_heading = boat.heading;
        boat.was_lean = boat.lean;
        const auto wet = [&](glm::vec2 p) {
            const glm::ivec2 t = water.tile_of(p);
            return water.at(t.x, t.y).has_value();
        };
        const i32 steps = static_cast<i32>(std::ceil(seconds / kStep.value));
        for (i32 step = 0; step < steps; ++step)
        {
            const f32 dt = std::min(seconds - static_cast<f32>(step) * kStep.value, kStep.value);
            boat.out.value += dt;
            boat.on_mark.value += dt;
            if (boat.state == Sailing::Out && boat.out.value >= roam)
            {
                boat.state = Sailing::Home;
                boat.marks.clear();
            }
            while (!boat.marks.empty() && glm::distance(boat.at, boat.marks.front()) < kReached.value)
            {
                boat.marks.erase(boat.marks.begin());
                boat.on_mark = { };
            }
            if (boat.state == Sailing::Home && boat.marks.empty() && !boat.calls.empty())
            {
                // On to the next dock by water, or straight for it when the water does not join them.
                boat.marks = marks_to(water, boat.at, boat.calls.front());
                if (boat.marks.empty())
                    boat.marks = { boat.calls.front() };
                boat.calls.erase(boat.calls.begin());
                boat.on_mark = { };
            }
            if (boat.state == Sailing::Home && boat.marks.empty())
            {
                boat.at = dock.enter;
                boat.speed = { };
                boat.state = Sailing::Docked;
                return true;
            }
            if (boat.state == Sailing::Out && (boat.marks.empty() || boat.on_mark > kGiveUp))
            {
                // Somewhere ahead, near enough; any water near when nothing ahead is.
                const glm::ivec2 here = water.tile_of(boat.at);
                boat.marks.clear();
                for (i32 k = 0; k < kTries && boat.marks.empty(); ++k)
                {
                    const glm::ivec2 pick = here + glm::ivec2 { static_cast<i32>(random::between(dice, i64 { -kReach }, i64 { kReach })), static_cast<i32>(random::between(dice, i64 { -kReach }, i64 { kReach })) };
                    if (pick == here || !water.at(pick.x, pick.y) || (k < kTries / 2 && glm::dot(water.middle(pick) - boat.at, boat.heading) <= 0))
                        continue;
                    boat.marks = marks_to(water, boat.at, water.middle(pick));
                }
                boat.on_mark = { };
            }
            // Steer for the mark, turning only so fast; turned round, the long way when only that side has water.
            f32 off = 0;
            if (!boat.marks.empty() && boat.marks.front() != boat.at)
            {
                off = angle_between(boat.heading, glm::normalize(boat.marks.front() - boat.at));
                const glm::vec2 left = turned(boat.heading, glm::half_pi<f32>());
                if (std::abs(off) > kTurnedRound * glm::pi<f32>())
                {
                    const bool wet_left = wet(boat.at + left * kBeam.value), wet_right = wet(boat.at - left * kBeam.value);
                    if (off > 0 && !wet_left && wet_right)
                        off -= glm::two_pi<f32>();
                    else if (off < 0 && !wet_right && wet_left)
                        off += glm::two_pi<f32>();
                }
            }
            // Another boat close ahead: it steers off to the side away from it, the harder the closer.
            if (!bumps)
                for (const Boat& other : fleet)
                {
                    const glm::vec2 to = other.at - boat.at;
                    const f32 apart = glm::length(to);
                    if (&other == &boat || other.state == Sailing::Docked || apart >= kKeepClear.value || apart <= 0 || glm::dot(to, boat.heading) <= 0)
                        continue;
                    const f32 side = boat.heading.x * to.y - boat.heading.y * to.x; // Over 0 on its left
                    off += (side > 0 ? -kDodge.value : kDodge.value) * (1 - apart / kKeepClear.value);
                }
            const f32 turning = std::clamp(handling.turn_gain * off, -handling.turn_most, handling.turn_most);
            const f32 lean = std::clamp(handling.lean_gain * turning, -handling.lean_most, handling.lean_most);
            boat.turning = { turning };
            boat.heading = glm::normalize(turned(boat.heading, turning * dt));
            boat.lean.value += (lean - boat.lean.value) * std::min(dt / kLeanSettle.value, 1.0f);
            // It gains speed only with its mark ahead, loses it the more it must turn, and brakes near its last mark
            // and for each way ahead that runs out of water.
            const f32 want = boat.marks.empty() ? 0.0f : top;
            f32 push = std::abs(off) < kAhead.value ? (want - boat.speed.value) * handling.speed_gain : 0.0f;
            push = std::clamp(push - handling.turn_slow * std::abs(off), -2 * handling.speed_up, handling.speed_up);
            if (boat.marks.size() == 1 && glm::distance(boat.at, boat.marks.front()) < kSlowNear.value)
                push -= handling.brake;
            for (const f32 side : { -kLookWide.value, 0.0f, kLookWide.value })
                if (!wet(boat.at + turned(boat.heading, side) * kLookAhead.value))
                    push -= handling.brake;
            boat.speed.value = std::max(boat.speed.value + push * dt, boat.marks.empty() ? 0.0f : kSlowest.value);
            boat.at += boat.heading * boat.speed.value * dt;
            // Touching another boat, it is pushed clear, onto water only; a bumping boat heading into it bounces off.
            for (const Boat& other : fleet)
            {
                const glm::vec2 away = boat.at - other.at;
                const f32 apart = glm::length(away);
                if (&other == &boat || other.state == Sailing::Docked || apart >= kHull.value)
                    continue;
                // Two boats on the very same spot, as they are leaving the dock together, part to either side.
                const glm::vec2 out = apart > 0.01f ? away / apart : turned(boat.heading, &boat < &other ? glm::half_pi<f32>() : -glm::half_pi<f32>());
                if (const glm::vec2 to = boat.at + out * (kHull.value - apart) / 2.0f; wet(to))
                    boat.at = to;
                if (bumps && glm::dot(boat.heading, out) < 0)
                {
                    boat.heading = glm::normalize(boat.heading - 2 * glm::dot(boat.heading, out) * out);
                    boat.speed.value *= kBounce;
                }
            }
        }
        return false;
    }

} // namespace rct3::simulation
