// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/chain_lifts.hpp"

#include <cmath>
#include <cstdlib>

namespace rct3::park
{

    Chains chain_runs(const save::RideModel& rides)
    {
        Chains runs;
        for (std::size_t r = 0; r < rides.rides.size(); ++r)
        {
            if (!rides.rides[r].track)
                continue;
            const auto run = static_cast<std::int32_t>(r + 1);
            for (const save::TrackPiece& piece : rides.rides[r].track->pieces)
                if (piece.scenery != 0)
                    runs[piece.scenery] = piece.reversed ? -run : run;
        }
        return runs;
    }

    float chain_shift(float shift, float speed, float seconds)
    {
        const float moved = shift - speed * seconds * kChainRepeatsPerMetre;
        return moved - std::floor(moved);
    }

    float chain_scroll(std::span<const float> shifts, std::int32_t run)
    {
        const auto ride = static_cast<std::size_t>(std::abs(run)) - 1;
        if (run == 0 || ride >= shifts.size())
            return 0;
        const float shift = run > 0 ? shifts[ride] : -shifts[ride];
        return shift - std::floor(shift);
    }

} // namespace rct3::park
