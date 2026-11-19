// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/attraction_objects.hpp"

#include "core/types.hpp"

namespace rct3::park
{

    AttractionObjects attraction_objects(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects)
    {
        // Placements first, then every object hanging from one takes its attraction.
        std::unordered_map<u64, AttractionRef> placements;
        AttractionObjects out;
        const auto own = [&](u64 placement, AttractionRef ref) {
            if (placement != 0)
                placements.emplace(placement, ref);
        };
        for (usize i = 0; i < rides.rides.size(); ++i)
        {
            const save::model::Ride& r = rides.rides[i];
            const AttractionRef ref { AttractionRef::Kind::Ride, i };
            own(r.base.placement, ref);
            for (const save::model::Station& s : r.stations)
            {
                own(s.entrance, ref);
                own(s.exit, ref);
            }
            if (r.track)
            {
                for (const save::model::TrackPiece& p : r.track->pieces)
                {
                    if (p.scenery != 0)
                        out.emplace(p.scenery, ref);
                }
            }
        }
        for (usize i = 0; i < rides.stalls.size(); ++i)
            own(rides.stalls[i].base.placement, { AttractionRef::Kind::Stall, i });
        for (const save::model::PlacedObject& o : objects)
        {
            if (const auto found = placements.find(o.owner_ref); found != placements.end())
                out.emplace(o.id, found->second);
        }
        return out;
    }

    std::vector<usize> twins(std::span<const save::model::PlacedObject> objects, std::span<const usize> of)
    {
        // By id, each of `of` and the id it hangs from.
        std::unordered_map<u64, usize> by_id;
        std::unordered_map<u64, usize> by_owner;
        for (const usize i : of)
        {
            if (i < objects.size())
            {
                by_id.emplace(objects[i].id, i);
                by_owner.emplace(objects[i].owner_ref, i);
            }
        }
        std::vector<usize> out;
        for (usize i = 0; i < objects.size(); ++i)
        {
            const save::model::PlacedObject& o = objects[i];
            const auto child = by_id.find(o.owner_ref);
            const auto parent = by_owner.find(o.id);
            for (const auto found : { child != by_id.end() ? child->second : objects.size(), parent != by_owner.end() ? parent->second : objects.size() })
            {
                if (found >= objects.size() || found == i)
                    continue;
                const save::model::PlacedObject& t = objects[found];
                if (t.symbol == o.symbol && t.overlay == o.overlay && t.col == o.col && t.row == o.row && t.quadrant == o.quadrant && t.rotation == o.rotation && t.base_height == o.base_height)
                {
                    out.push_back(i);
                    break;
                }
            }
        }
        return out;
    }

} // namespace rct3::park
