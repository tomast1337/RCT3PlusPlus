// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/attraction_objects.hpp"

namespace rct3::park
{

    AttractionObjects attraction_objects(const save::RideModel& rides, std::span<const save::PlacedObject> objects)
    {
        // Placements first, then every object hanging from one takes its attraction.
        std::unordered_map<std::uint64_t, AttractionRef> placements;
        AttractionObjects out;
        const auto own = [&](std::uint64_t placement, AttractionRef ref) {
            if (placement != 0)
                placements.emplace(placement, ref);
        };
        for (std::size_t i = 0; i < rides.rides.size(); ++i)
        {
            const save::Ride& r = rides.rides[i];
            const AttractionRef ref { AttractionRef::Kind::Ride, i };
            own(r.base.placement, ref);
            for (const save::Station& s : r.stations)
            {
                own(s.entrance, ref);
                own(s.exit, ref);
            }
            if (r.track)
            {
                for (const save::TrackPiece& p : r.track->pieces)
                {
                    if (p.scenery != 0)
                        out.emplace(p.scenery, ref);
                }
            }
        }
        for (std::size_t i = 0; i < rides.stalls.size(); ++i)
            own(rides.stalls[i].base.placement, { AttractionRef::Kind::Stall, i });
        for (const save::PlacedObject& o : objects)
        {
            if (const auto found = placements.find(o.owner_ref); found != placements.end())
                out.emplace(o.id, found->second);
        }
        return out;
    }

} // namespace rct3::park
