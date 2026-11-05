// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pool_waves.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace rct3::park
{

    namespace
    {

        constexpr std::array<glm::vec2, 4> kSide { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } }; // By turn: towards the next row, next column, ...

        std::optional<save::Tile> tile_at(const save::TerrainGrid& grid, glm::vec2 at)
        {
            if (grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
                return std::nullopt;
            return save::Tile { static_cast<int>(std::floor((at.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((at.y - grid.origin_z) / grid.tile_size_z)) };
        }

        /// The box round the water squares joined side to side, at one surface, to the water square at `start`.
        glm::vec4 body_bounds(const PoolMap& map, const save::TerrainGrid& grid, const PoolSquare& start)
        {
            std::vector<save::Tile> body { { start.col, start.row } };
            for (std::size_t i = 0; i < body.size(); ++i)
                for (const glm::vec2 side : kSide)
                {
                    const save::Tile next { body[i].col + static_cast<int>(side.x), body[i].row + static_cast<int>(side.y) };
                    if (const auto square = map.at(next.col, next.row, start.surface); square && square->kind == PoolKind::Water && square->surface == start.surface && !std::ranges::contains(body, next))
                        body.push_back(next);
                }
            glm::ivec2 low { start.col, start.row };
            glm::ivec2 high = low;
            for (const save::Tile& t : body)
            {
                low = glm::min(low, glm::ivec2 { t.col, t.row });
                high = glm::max(high, glm::ivec2 { t.col, t.row });
            }
            return { grid.world_x(low.x), grid.world_z(low.y), grid.world_x(high.x + 1), grid.world_z(high.y + 1) };
        }

        bool inside(const glm::vec4& bounds, glm::vec2 at)
        {
            return at.x >= bounds.x && at.y >= bounds.y && at.x <= bounds.z && at.y <= bounds.w;
        }

    } // namespace

    bool on_pool_water(const PoolMap& map, const save::TerrainGrid& grid, glm::vec2 at)
    {
        const auto tile = tile_at(grid, at);
        const auto square = tile ? map.at(tile->col, tile->row) : std::nullopt;
        return square && square->kind == PoolKind::Water;
    }

    std::vector<PoolWave> machine_waves(const PoolMap& map, std::span<const PoolExtra> scenery, const save::TerrainGrid& grid)
    {
        std::vector<PoolWave> out;
        std::vector<std::vector<save::Tile>> complexes; // By room, found once a machine needs them
        const auto add = [&](const PoolExtra& e) {
            const auto square = map.at(e.col, e.row);
            if (!e.fit.wave_machine || !square || square->kind != PoolKind::Water)
                return;
            if (complexes.empty())
                for (std::size_t i = 0; i < map.rooms.size(); ++i)
                    complexes.push_back(pool_complex(map, i));
            PoolWave& w = out.emplace_back();
            w.kind = PoolWaveKind::Machine;
            const glm::vec2 side { e.side.step()[0], e.side.step()[1] };
            const glm::vec2 half { grid.tile_size_x / 2, grid.tile_size_z / 2 };
            w.from = glm::vec2 { grid.world_x(e.col), grid.world_z(e.row) } + half + side * half;
            w.heading = -side;
            w.radius = glm::length(glm::vec2 { side.y, side.x } * half); // Half the square, along the wall
            w.bounds = body_bounds(map, grid, *square);
            for (std::size_t i = 0; i < complexes.size(); ++i)
                if (std::ranges::contains(complexes[i], save::Tile { e.col, e.row }))
                {
                    w.settings = map.rooms[i].waves;
                    break;
                }
        };
        std::ranges::for_each(map.extras, add);
        std::ranges::for_each(scenery, add);
        return out;
    }

    std::optional<PoolWave> drawn_wave(const PoolMap& map, const save::TerrainGrid& grid, std::span<const glm::vec3> points, float clock, const PoolWaveLook& look)
    {
        if (points.size() < 3)
            return std::nullopt;
        const auto tile = tile_at(grid, { points.front().x, points.front().y });
        const auto square = tile ? map.at(tile->col, tile->row) : std::nullopt;
        if (!square || square->kind != PoolKind::Water)
            return std::nullopt;
        // The way the pointer went, how far it turned all told (one way less the other), and the box round it.
        float stroke = 0;
        float turning = 0;
        glm::vec2 low { points.front() };
        glm::vec2 high = low;
        std::optional<glm::vec2> last_step;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const glm::vec2 step = glm::vec2 { points[i] } - glm::vec2 { points[i - 1] };
            low = glm::min(low, glm::vec2 { points[i] });
            high = glm::max(high, glm::vec2 { points[i] });
            const float length = glm::length(step);
            if (length < 1e-3f)
                continue;
            stroke += length;
            if (last_step)
                turning += std::atan2(last_step->x * step.y - last_step->y * step.x, glm::dot(*last_step, step));
            last_step = step;
        }
        if (stroke < look.least_stroke)
            return std::nullopt;
        PoolWave w;
        w.bounds = body_bounds(map, grid, *square);
        w.start = clock;
        const glm::vec2 size = high - low;
        if (std::abs(turning) >= look.least_turning && std::min(size.x, size.y) >= look.round * std::max(size.x, size.y))
        {
            w.kind = PoolWaveKind::Whirlpool;
            w.from = (low + high) / 2.0f;
            float distance = 0;
            for (const glm::vec3& p : points)
                distance += glm::distance(glm::vec2 { p }, w.from);
            w.radius = std::clamp(distance / static_cast<float>(points.size()), look.least_radius, look.most_radius);
            w.spin = turning > 0 ? 1.0f : -1.0f;
            return w;
        }
        const glm::vec2 chord = glm::vec2 { points.back() } - glm::vec2 { points.front() };
        if (glm::length(chord) < look.straight * stroke)
            return std::nullopt;
        const bool along_x = std::abs(chord.x) >= std::abs(chord.y);
        if (std::atan2(along_x ? std::abs(chord.y) : std::abs(chord.x), along_x ? std::abs(chord.x) : std::abs(chord.y)) > look.aslant)
            return std::nullopt;
        w.kind = PoolWaveKind::Line;
        w.from = glm::vec2 { points.front() };
        w.heading = along_x ? glm::vec2 { chord.x > 0 ? 1.0f : -1.0f, 0.0f } : glm::vec2 { 0.0f, chord.y > 0 ? 1.0f : -1.0f };
        return w;
    }

    void drop_spent_waves(std::vector<PoolWave>& waves, float clock, const PoolWaveLook& look)
    {
        std::erase_if(waves, [&](const PoolWave& w) { return (w.kind == PoolWaveKind::Line && clock - w.start > look.line_life) || (w.kind == PoolWaveKind::Whirlpool && clock - w.start > look.whirl_life); });
    }

    float pool_wave_height(std::span<const PoolWave> waves, glm::vec2 at, float clock, const PoolWaveLook& look)
    {
        float sum = 0;
        for (const PoolWave& w : waves.first(std::min(waves.size(), kMaxPoolWaves)))
        {
            if (!inside(w.bounds, at))
                continue;
            const float d = glm::dot(at - w.from, w.heading);
            const float age = clock - w.start;
            if (w.kind == PoolWaveKind::Machine)
            {
                // When the water at d left the wall, and whether the machine ran then, easing in and out.
                const PoolWaves& s = w.settings;
                const float left = clock - d / look.speed;
                if (d < 0 || d > look.cut || left < 0 || s.on <= 0)
                    continue;
                // Its front, wider out from the wall and lower for it, fading out at its sides.
                const float half_front = w.radius + look.spread * d;
                const float out = std::abs(glm::dot(at - w.from, glm::vec2 { -w.heading.y, w.heading.x }));
                const float across = std::clamp((half_front + look.front_soft - out) / (2 * look.front_soft), 0.0f, 1.0f) * std::sqrt(w.radius / half_front);
                const float phase = std::fmod(left, std::max({ s.every, s.on, 1.0f }));
                const float running = std::clamp(phase / look.ramp, 0.0f, 1.0f) * std::clamp((s.on - phase) / look.ramp, 0.0f, 1.0f);
                sum += look.crest * s.height * running * std::sin(s.frequency * left) * std::exp(-d / look.reach) * across * std::clamp(look.cut - d, 0.0f, 1.0f);
            }
            else if (w.kind == PoolWaveKind::Line && age >= 0 && age <= look.line_life)
            {
                const float x = d - look.line_speed * age;
                sum += look.line_height * std::exp(-age / look.line_fade) * std::exp(-(x / look.line_width) * (x / look.line_width)) * std::cos(2 * std::numbers::pi_v<float> / look.line_length * x);
            }
            else if (w.kind == PoolWaveKind::Whirlpool && age >= 0 && age <= look.whirl_life)
            {
                const float strength = std::clamp(age, 0.0f, 1.0f) * std::clamp((look.whirl_life - age) / 3, 0.0f, 1.0f);
                const glm::vec2 off = at - w.from;
                const float r = glm::length(off) / w.radius;
                const float arms = look.whirl_arms * r * std::sin(3 * std::atan2(off.y, off.x) - w.spin * (look.whirl_turn * age - look.whirl_wind * r));
                sum -= look.whirl_depth * strength * std::exp(-r * r) * (1 - arms);
            }
        }
        return sum;
    }

    float pool_wave_lift(float height, float room, const PoolWaveLook& look)
    {
        return std::min(height * std::clamp(room / look.edge, 0.0f, 1.0f), look.rim_room + look.rim_rise * room);
    }

    PoolWaveTexels pool_wave_texels(std::span<const PoolWave> waves, const PoolWaveLook& look)
    {
        PoolWaveTexels texels { };
        for (std::size_t i = 0; i < std::min(waves.size(), kMaxPoolWaves); ++i)
        {
            const PoolWave& w = waves[i];
            const float crest = w.kind == PoolWaveKind::Machine ? look.crest * w.settings.height : w.kind == PoolWaveKind::Line ? look.line_height : look.whirl_depth;
            texels[i] = { w.from.x, w.from.y, w.heading.x, w.heading.y };
            texels[kMaxPoolWaves + i] = { static_cast<float>(std::to_underlying(w.kind)), crest, w.start, w.spin };
            texels[2 * kMaxPoolWaves + i] = w.bounds;
            texels[3 * kMaxPoolWaves + i] = { w.settings.frequency, w.settings.on, w.settings.every, w.radius };
        }
        return texels;
    }

} // namespace rct3::park
