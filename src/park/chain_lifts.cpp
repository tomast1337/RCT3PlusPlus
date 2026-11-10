// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/chain_lifts.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rct3::park
{

    object::Chains chain_runs(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models)
    {
        std::map<std::uint64_t, const object::ObjectModel*> by_id;
        for (const save::model::Ride& ride : rides.rides)
            if (ride.track)
                for (const save::model::TrackPiece& piece : ride.track->pieces)
                    by_id.emplace(piece.scenery, nullptr);
        for (const save::model::PlacedObject& o : objects)
            if (const auto at = by_id.find(o.id); at != by_id.end())
                if (const auto model = models.find(object::model_ref(o)); model != models.end())
                    at->second = &model->second;
        object::Chains runs;
        for (std::size_t r = 0; r < rides.rides.size(); ++r)
        {
            if (!rides.rides[r].track)
                continue;
            const auto run = static_cast<std::int32_t>(r + 1);
            for (const save::model::TrackPiece& piece : rides.rides[r].track->pieces)
            {
                if (piece.scenery == 0)
                    continue;
                const auto model = by_id.find(piece.scenery);
                const std::string_view section = std::string_view(piece.symbol).substr(0, piece.symbol.find(':'));
                const bool back = model != by_id.end() && model->second != nullptr && std::ranges::any_of(model->second->runs_back, [&](const std::string& s) { return strings::equals_ignoring_case(s, section); });
                runs[piece.scenery] = piece.reversed != back ? -run : run;
            }
        }
        return runs;
    }

    float chain_shift(float shift, float speed, float seconds)
    {
        const float moved = shift - speed * seconds * kChainRepeatsPerMetre;
        return moved - std::floor(moved);
    }

    math::TextureShift chain_scroll(std::span<const float> shifts, std::int32_t run)
    {
        const auto ride = static_cast<std::size_t>(std::abs(run)) - 1;
        if (run == 0 || ride >= shifts.size())
            return { };
        return math::TextureShift::by(run > 0 ? shifts[ride] : -shifts[ride]);
    }

} // namespace rct3::park
