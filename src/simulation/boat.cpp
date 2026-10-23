// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/boat.hpp"

#include "park/water_fill.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace rct3::simulation
{

    namespace
    {
        // Picked to look right on a park lake.
        constexpr float kStep = 0.1f;     ///< Seconds per part step
        constexpr float kTurnRate = 1.2f; ///< Radians a second at most
        constexpr float kReached = 2.5f;  ///< Metres from a mark that count as there
        constexpr float kGiveUp = 12;     ///< Seconds on one mark before it picks another
        constexpr float kSlowest = 1;     ///< m/s it keeps to, however hard it turns
        constexpr float kSpeedUp = 0.8f;  ///< m/s² towards the speed it wants
        constexpr int kReach = 6;         ///< Tiles away it may pick its next mark
        constexpr int kTries = 20;        ///< Picks before it settles for any water
        constexpr float kSlowNear = 6;    ///< Metres from its last mark it starts to slow

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

    WaterTiles water_tiles(const save::TerrainGrid& grid, std::span<const save::WaterBody> bodies)
    {
        WaterTiles w { .cols = grid.width, .rows = grid.height, .origin_x = grid.origin_x, .origin_z = grid.origin_z, .tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : 4 };
        w.level.assign(static_cast<std::size_t>(std::max(grid.width, 0)) * static_cast<std::size_t>(std::max(grid.height, 0)), std::nullopt);
        for (const save::WaterBody& body : bodies)
        {
            // Both triangles with all three corners under the surface.
            std::vector<std::uint8_t> under(w.level.size(), 0);
            for (const save::WaterTri& t : body.tris)
                if (t.col < w.cols && t.row < w.rows && t.mask == 0b111)
                    under[static_cast<std::size_t>(t.row) * static_cast<std::size_t>(w.cols) + t.col] |= static_cast<std::uint8_t>(1u << (t.triangle & 1));
            for (std::size_t i = 0; i < under.size(); ++i)
                if (under[i] == 0b11)
                    w.level[i] = body.level;
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

    void cast_off(Boat& boat, const Dock& dock)
    {
        boat.at = dock.leave;
        boat.heading = glm::length(dock.heading) > 0 ? glm::normalize(dock.heading) : glm::vec2 { 0, 1 };
        boat.speed = kSlowest;
        boat.state = Sailing::Out;
        boat.marks.clear();
        boat.out = 0;
        boat.on_mark = 0;
    }

    bool sail(Boat& boat, const WaterTiles& water, const Dock& dock, float top, float roam, float seconds, random::RandomState& dice)
    {
        if (boat.state == Sailing::Docked)
            return false;
        const int steps = static_cast<int>(std::ceil(seconds / kStep));
        for (int step = 0; step < steps; ++step)
        {
            const float dt = std::min(seconds - static_cast<float>(step) * kStep, kStep);
            boat.out += dt;
            boat.on_mark += dt;
            if (boat.state == Sailing::Out && boat.out >= roam)
            {
                // Time's up: home by water, or straight for the dock when the water does not join it.
                boat.state = Sailing::Home;
                boat.marks = marks_to(water, boat.at, dock.enter);
                if (boat.marks.empty())
                    boat.marks = { dock.enter };
                boat.on_mark = 0;
            }
            while (!boat.marks.empty() && glm::distance(boat.at, boat.marks.front()) < kReached)
            {
                boat.marks.erase(boat.marks.begin());
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
            // Steer for the mark, turning only so fast, slower in a hard turn and near the last mark.
            float want = top;
            if (!boat.marks.empty())
            {
                const glm::vec2 to = boat.marks.front() - boat.at;
                if (glm::length(to) > 0)
                {
                    const float turn = angle_between(boat.heading, glm::normalize(to));
                    boat.heading = glm::normalize(turned(boat.heading, std::clamp(turn, -kTurnRate * dt, kTurnRate * dt)));
                    want *= 1 - 0.6f * std::min(std::abs(turn) / 3.1416f, 1.0f);
                }
                if (boat.marks.size() == 1)
                    want = std::min(want, top * glm::distance(boat.at, boat.marks.front()) / kSlowNear);
            }
            else
                want = 0;
            want = std::max(want, boat.marks.empty() ? 0.0f : kSlowest);
            boat.speed += std::clamp(want - boat.speed, -kSpeedUp * dt, kSpeedUp * dt);
            boat.at += boat.heading * boat.speed * dt;
        }
        return false;
    }

} // namespace rct3::simulation
