// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pool_score.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace rct3::park
{

    PoolTally pool_tally(std::span<const PoolSquare> squares, std::span<const PoolExtra> extras)
    {
        std::set<std::pair<std::int32_t, std::int32_t>> water;
        std::set<std::pair<std::int32_t, std::int32_t>> all;
        for (const PoolSquare& s : squares)
        {
            all.emplace(s.col, s.row);
            if (s.kind == PoolKind::Water)
                water.emplace(s.col, s.row);
        }
        PoolTally t;
        for (const PoolSquare& s : squares)
        {
            ++t.squares;
            t.depth_sum += s.depth;
            t.depth_sum2 += s.depth * s.depth;
            t.jacuzzis += s.kind == PoolKind::Jacuzzi ? 1 : 0;
            t.high_dives += s.kind == PoolKind::HighDive ? 1 : 0;
            if (s.kind != PoolKind::Water)
                continue;
            ++t.water;
            // Dry on two sides that meet: a corner jutting out.
            const auto dry = [&](std::int32_t dc, std::int32_t dr) { return !water.contains({ s.col + dc, s.row + dr }); };
            const bool corner = (dry(0, 1) || dry(0, -1)) && (dry(1, 0) || dry(-1, 0));
            t.shaped += s.edge != PoolEdge::Straight || corner ? 1 : 0;
        }
        for (const PoolExtra& e : extras)
        {
            if (!all.contains({ e.col, e.row }))
                continue;
            t.lamps += e.fit.light ? 1 : 0;
            t.ladders += e.fit.ladder ? 1 : 0;
            t.lifeguards += e.fit.lifeguard ? 1 : 0;
            t.loungers += e.fit.lounger ? 1 : 0;
            t.showers += e.fit.shower ? 1 : 0;
        }
        return t;
    }

    float pool_excitement(const PoolTally& t, float dirt)
    {
        if (t.squares == 0)
            return 0;
        const auto share = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
        const float n = static_cast<float>(t.squares);
        const float mean = static_cast<float>(t.depth_sum) / n;
        const float spread = std::sqrt(std::max(0.0f, static_cast<float>(t.depth_sum2) / n - mean * mean));
        // Chosen by eye: each term full at about what a well kept pool of fair size has, the weights adding to 1.
        float e = 0.30f * share(static_cast<float>(t.water) / 40) // 40 squares of water
            + 0.15f * share(spread / 0.8f)                        // shallow, normal and deep ends
            + 0.10f * share(static_cast<float>(t.lamps) * 6 / n); // a lamp to six squares
        if (t.water > 0)
        {
            const float w = static_cast<float>(t.water);
            e += 0.15f * share(static_cast<float>(t.shaped) * 3 / w)        // a third of the water shaped
                + 0.15f * share(static_cast<float>(t.ladders) * 12 / w)     // a ladder to twelve squares of water
                + 0.15f * share(static_cast<float>(t.lifeguards) * 30 / w); // a lifeguard to thirty
        }
        // Chosen by eye: a pool that is a health hazard is half as good.
        return share(e * (1 - share(dirt) / 2));
    }

    std::int64_t pool_worth(const PoolTally& t, std::size_t slides, std::size_t rivers, float dirt)
    {
        // Chosen by eye, hundredths: over the ticket new complexes start at, so a plain pool still sells at it.
        const auto up_to = [](std::int64_t count, std::int64_t most, std::int64_t each) { return std::min(count, most) * each; };
        return 300 + std::llround(500 * pool_excitement(t, dirt)) + up_to(t.jacuzzis, 4, 60) + up_to(t.high_dives, 3, 100) + up_to(static_cast<std::int64_t>(slides), 4, 150) + up_to(static_cast<std::int64_t>(rivers), 2, 200) + up_to(t.loungers, 8, 15) + up_to(t.showers, 4, 20);
    }

} // namespace rct3::park
