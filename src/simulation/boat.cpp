// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/boat.hpp"

#include "park/water/fill.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace rct3::simulation
{

    namespace
    {
        // Picked to look right on a park lake.
        constexpr float kStep = 0.1f;       ///< Seconds per part step
        constexpr float kReached = 2.5f;    ///< Metres from a mark that count as there
        constexpr float kGiveUp = 12;       ///< Seconds on one mark before it picks another
        constexpr float kSlowest = 1;       ///< m/s it keeps to, however hard it turns
        constexpr int kReach = 6;           ///< Tiles away it may pick its next mark
        constexpr int kTries = 20;          ///< Picks before it settles for any water
        constexpr float kSlowNear = 6;      ///< Metres from its last mark it starts to brake
        constexpr float kAhead = 0.5f;      ///< Radians off its bow its mark may lie for it to speed up
        constexpr float kLookAhead = 4;     ///< Metres ahead it looks for water to go on in
        constexpr float kLookWide = 0.5f;   ///< Radians either side of its bow it looks too
        constexpr float kBeam = 2;          ///< Metres to its side it looks for water to turn round in
        constexpr float kLeanSettle = 0.5f; ///< Seconds its lean takes to follow its turn
        constexpr float kHull = 2;          ///< Metres apart two boats' middles touch at
        constexpr float kKeepClear = 10;    ///< Metres ahead it starts to steer round another boat
        constexpr float kDodge = 2;         ///< Radians it steers off its mark, at most, to keep clear
        constexpr float kBounce = 0.6f;     ///< Of its speed a bumping boat keeps through a bump

        float angle_between(glm::vec2 from, glm::vec2 to)
        {
            return std::atan2(from.x * to.y - from.y * to.x, glm::dot(from, to));
        }

        glm::vec2 turned(glm::vec2 v, float angle)
        {
            const float c = std::cos(angle), s = std::sin(angle);
            return { v.x * c - v.y * s, v.x * s + v.y * c };
        }

        /// Marks along the route to `goal`, `goal` last; empty when the water does not reach it.
        std::vector<glm::vec2> marks_to(const WaterTiles& water, glm::vec2 from, glm::vec2 goal)
        {
            const auto way = water_route(water, water.tile_of(from), water.tile_of(goal));
            if (!way)
                return { };
            std::vector<glm::vec2> marks;
            for (std::size_t i = 0; i + 1 < way->size(); ++i)
                marks.push_back(water.middle((*way)[i]));
            marks.push_back(goal);
            return marks;
        }
    } // namespace

    std::optional<float> WaterTiles::at(int col, int row) const
    {
        if (col < 0 || row < 0 || col >= cols || row >= rows)
            return std::nullopt;
        return level[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col)];
    }

    glm::ivec2 WaterTiles::tile_of(glm::vec2 p) const
    {
        return { static_cast<int>(std::floor((p.x - origin_x) / tile_size)), static_cast<int>(std::floor((p.y - origin_z) / tile_size)) };
    }

    glm::vec2 WaterTiles::middle(glm::ivec2 tile) const
    {
        return { origin_x + (static_cast<float>(tile.x) + 0.5f) * tile_size, origin_z + (static_cast<float>(tile.y) + 0.5f) * tile_size };
    }

    WaterTiles water_tiles(const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> bodies, const park::path::SolidTiles& solids)
    {
        WaterTiles w { .cols = grid.width, .rows = grid.height, .origin_x = grid.origin_x, .origin_z = grid.origin_z, .tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : 4 };
        w.level.assign(static_cast<std::size_t>(std::max(grid.width, 0)) * static_cast<std::size_t>(std::max(grid.height, 0)), std::nullopt);
        for (const save::model::WaterBody& body : bodies)
        {
            // Both triangles with all three corners under the surface.
            std::vector<std::uint8_t> under(w.level.size(), 0);
            for (const save::model::WaterTri& t : body.tris)
                if (t.col < w.cols && t.row < w.rows && t.mask == 0b111)
                    under[static_cast<std::size_t>(t.row) * static_cast<std::size_t>(w.cols) + t.col] |= static_cast<std::uint8_t>(1u << (t.triangle & 1));
            for (std::size_t i = 0; i < under.size(); ++i)
                if (under[i] == 0b11)
                    w.level[i] = body.level;
        }
        // Closed where rocks, scenery and the like stand in the water or hang lower over it than a boat clears;
        // what stands only along a tile's edges a boat slips past.
        constexpr float kUnder = 1; ///< Metres under the surface something may reach up from
        constexpr float kClear = 1; ///< Metres over the surface a boat needs
        for (const auto& [key, spans] : solids.tiles)
        {
            const int col = static_cast<int>(key & 0xFFFF), row = static_cast<int>(key >> 16);
            const auto level = w.at(col, row);
            if (level && std::ranges::any_of(spans, [&](const park::path::SolidSpan& s) { return (s.room & park::path::kWholeTile) != 0 && s.low < *level + kClear && *level - kUnder < s.high; }))
                w.level[static_cast<std::size_t>(row) * static_cast<std::size_t>(w.cols) + static_cast<std::size_t>(col)] = std::nullopt;
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
        const auto index = [&](glm::ivec2 t) { return static_cast<std::size_t>(t.y) * static_cast<std::size_t>(water.cols) + static_cast<std::size_t>(t.x); };
        std::vector<int> next(water.level.size(), -1);
        std::deque<glm::ivec2> open { to };
        next[index(to)] = static_cast<int>(index(to));
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
                next[index(n)] = static_cast<int>(index(t));
                open.push_back(n);
            }
        }
        if (next[index(from)] < 0)
            return std::nullopt;
        std::vector<glm::ivec2> way;
        for (std::size_t at = static_cast<std::size_t>(next[index(from)]);; at = static_cast<std::size_t>(next[at]))
        {
            way.push_back({ static_cast<int>(at % static_cast<std::size_t>(water.cols)), static_cast<int>(at / static_cast<std::size_t>(water.cols)) });
            if (at == index(to))
                break;
        }
        return way;
    }

    BoatPose between_sails(const Boat& boat, float along)
    {
        const float turn = angle_between(boat.was_heading, boat.heading);
        return { glm::mix(boat.was, boat.at, along), turned(boat.was_heading, turn * along), glm::mix(boat.was_lean, boat.lean, along) };
    }

    Stroke stroke(float turning)
    {
        return turning > kRowRound ? Stroke::Left : turning < -kRowRound ? Stroke::Right : Stroke::Both;
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
        boat.out = 0;
        boat.on_mark = 0;
        boat.turning = 0;
        boat.lean = 0;
        boat.was = boat.at;
        boat.was_heading = boat.heading;
        boat.was_lean = 0;
    }

    bool sail(Boat& boat, const WaterTiles& water, const Dock& dock, const ovl::BoatHandling& handling, float top, float roam, float seconds, random::RandomState& dice, std::span<const Boat> fleet, bool bumps)
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
        const int steps = static_cast<int>(std::ceil(seconds / kStep));
        for (int step = 0; step < steps; ++step)
        {
            const float dt = std::min(seconds - static_cast<float>(step) * kStep, kStep);
            boat.out += dt;
            boat.on_mark += dt;
            if (boat.state == Sailing::Out && boat.out >= roam)
            {
                boat.state = Sailing::Home;
                boat.marks.clear();
            }
            while (!boat.marks.empty() && glm::distance(boat.at, boat.marks.front()) < kReached)
            {
                boat.marks.erase(boat.marks.begin());
                boat.on_mark = 0;
            }
            if (boat.state == Sailing::Home && boat.marks.empty() && !boat.calls.empty())
            {
                // On to the next dock by water, or straight for it when the water does not join them.
                boat.marks = marks_to(water, boat.at, boat.calls.front());
                if (boat.marks.empty())
                    boat.marks = { boat.calls.front() };
                boat.calls.erase(boat.calls.begin());
                boat.on_mark = 0;
            }
            if (boat.state == Sailing::Home && boat.marks.empty())
            {
                boat.at = dock.enter;
                boat.speed = 0;
                boat.state = Sailing::Docked;
                return true;
            }
            if (boat.state == Sailing::Out && (boat.marks.empty() || boat.on_mark > kGiveUp))
            {
                // Somewhere ahead, near enough; any water near when nothing ahead is.
                const glm::ivec2 here = water.tile_of(boat.at);
                boat.marks.clear();
                for (int k = 0; k < kTries && boat.marks.empty(); ++k)
                {
                    const glm::ivec2 pick = here + glm::ivec2 { static_cast<int>(random::between(dice, std::int64_t { -kReach }, std::int64_t { kReach })), static_cast<int>(random::between(dice, std::int64_t { -kReach }, std::int64_t { kReach })) };
                    if (pick == here || !water.at(pick.x, pick.y) || (k < kTries / 2 && glm::dot(water.middle(pick) - boat.at, boat.heading) <= 0))
                        continue;
                    boat.marks = marks_to(water, boat.at, water.middle(pick));
                }
                boat.on_mark = 0;
            }
            // Steer for the mark, turning only so fast; turned round, the long way when only that side has water.
            float off = 0;
            if (!boat.marks.empty() && boat.marks.front() != boat.at)
            {
                off = angle_between(boat.heading, glm::normalize(boat.marks.front() - boat.at));
                const glm::vec2 left = turned(boat.heading, glm::half_pi<float>());
                if (std::abs(off) > 0.75f * glm::pi<float>())
                {
                    const bool wet_left = wet(boat.at + left * kBeam), wet_right = wet(boat.at - left * kBeam);
                    if (off > 0 && !wet_left && wet_right)
                        off -= glm::two_pi<float>();
                    else if (off < 0 && !wet_right && wet_left)
                        off += glm::two_pi<float>();
                }
            }
            // Another boat close ahead: it steers off to the side away from it, the harder the closer.
            if (!bumps)
                for (const Boat& other : fleet)
                {
                    const glm::vec2 to = other.at - boat.at;
                    const float apart = glm::length(to);
                    if (&other == &boat || other.state == Sailing::Docked || apart >= kKeepClear || apart <= 0 || glm::dot(to, boat.heading) <= 0)
                        continue;
                    const float side = boat.heading.x * to.y - boat.heading.y * to.x; // Over 0 on its left
                    off += (side > 0 ? -kDodge : kDodge) * (1 - apart / kKeepClear);
                }
            boat.turning = std::clamp(handling.turn_gain * off, -handling.turn_most, handling.turn_most);
            boat.heading = glm::normalize(turned(boat.heading, boat.turning * dt));
            boat.lean += (std::clamp(handling.lean_gain * boat.turning, -handling.lean_most, handling.lean_most) - boat.lean) * std::min(dt / kLeanSettle, 1.0f);
            // It gains speed only with its mark ahead, loses it the more it must turn, and brakes near its last mark
            // and for each way ahead that runs out of water.
            const float want = boat.marks.empty() ? 0.0f : top;
            float push = std::abs(off) < kAhead ? (want - boat.speed) * handling.speed_gain : 0.0f;
            push = std::clamp(push - handling.turn_slow * std::abs(off), -2 * handling.speed_up, handling.speed_up);
            if (boat.marks.size() == 1 && glm::distance(boat.at, boat.marks.front()) < kSlowNear)
                push -= handling.brake;
            for (const float side : { -kLookWide, 0.0f, kLookWide })
                if (!wet(boat.at + turned(boat.heading, side) * kLookAhead))
                    push -= handling.brake;
            boat.speed = std::max(boat.speed + push * dt, boat.marks.empty() ? 0.0f : kSlowest);
            boat.at += boat.heading * boat.speed * dt;
            // Touching another boat, it is pushed clear, onto water only; a bumping boat heading into it bounces off.
            for (const Boat& other : fleet)
            {
                const glm::vec2 away = boat.at - other.at;
                const float apart = glm::length(away);
                if (&other == &boat || other.state == Sailing::Docked || apart >= kHull)
                    continue;
                // Two boats on the very same spot, as they are leaving the dock together, part to either side.
                const glm::vec2 out = apart > 0.01f ? away / apart : turned(boat.heading, &boat < &other ? glm::half_pi<float>() : -glm::half_pi<float>());
                if (const glm::vec2 to = boat.at + out * (kHull - apart) / 2.0f; wet(to))
                    boat.at = to;
                if (bumps && glm::dot(boat.heading, out) < 0)
                {
                    boat.heading = glm::normalize(boat.heading - 2 * glm::dot(boat.heading, out) * out);
                    boat.speed *= kBounce;
                }
            }
        }
        return false;
    }

} // namespace rct3::simulation
