// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride/cars.hpp"

#include "core/types.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace rct3::park::ride
{

    std::vector<usize> car_slots(i32 cars, const std::array<bool, 6>& filled)
    {
        const usize middle = filled[2] ? 2 : 0;
        std::vector<usize> out;
        for (i32 k = 0; k < cars; ++k)
        {
            if (k > 0 && filled[5])
                out.push_back(5);
            const usize own = k == 0 ? 0 : k == cars - 1 ? 4 : k == 1 ? 1 : k == cars - 2 ? 3 : 2;
            out.push_back(filled[own] ? own : middle);
        }
        return out;
    }

    RideCars ride_cars(std::span<const save::model::Ride> rides, u64 last_id)
    {
        constexpr std::string_view kTrainTag = ":rit";
        RideCars out;
        for (usize r = 0; r < rides.size(); ++r)
        {
            const save::model::Ride& ride = rides[r];
            for (usize t = 0; t < ride.trains.size(); ++t)
            {
                const save::model::Train& train = ride.trains[t];
                std::string name = train.symbol;
                if (name.ends_with(kTrainTag))
                    name.resize(name.size() - kTrainTag.size());
                for (const save::model::Car& car : train.cars)
                {
                    if (!car.position_valid || car.design_slot < 0 || name.empty())
                        continue;
                    save::model::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.overlay = train.overlay_path;
                    o.symbol = std::format("{}#{}", name, car.design_slot);
                    o.colours = { ride.car_colours[0], ride.car_colours[1], ride.car_colours[2] };
                    out.designs.push_back({ name, static_cast<usize>(car.design_slot) });
                    out.spots.push_back({ save::model::RideIndex { r }, t, car.front_wheel_distance, car.rear_wheel_distance });
                    glm::mat4 placed = glm::translate(glm::mat4(1.0f), { car.position[0], car.position[1], car.position[2] });
                    const glm::vec3 turn { car.orientation[0], car.orientation[1], car.orientation[2] };
                    if (const f32 angle = glm::length(turn); angle > 1e-6f)
                        placed = glm::rotate(placed, angle, turn / angle);
                    out.fitted.emplace(o.id, placed);
                }
            }
        }
        return out;
    }

} // namespace rct3::park::ride
