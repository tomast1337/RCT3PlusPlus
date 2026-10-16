// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_cars.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace rct3::park
{

    RideCars ride_cars(std::span<const save::Ride> rides, std::uint64_t last_id)
    {
        constexpr std::string_view kTrainTag = ":rit";
        RideCars out;
        for (const save::Ride& ride : rides)
        {
            for (const save::Train& train : ride.trains)
            {
                std::string name = train.symbol;
                if (name.ends_with(kTrainTag))
                    name.resize(name.size() - kTrainTag.size());
                for (const save::Car& car : train.cars)
                {
                    if (!car.position_valid || car.design_slot < 0 || name.empty())
                        continue;
                    save::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.overlay = train.overlay_path;
                    o.symbol = name + "#" + std::to_string(car.design_slot);
                    o.colours = { ride.car_colours[0], ride.car_colours[1], ride.car_colours[2] };
                    out.designs.push_back({ name, static_cast<std::size_t>(car.design_slot) });
                    glm::mat4 placed = glm::translate(glm::mat4(1.0f), { car.position[0], car.position[1], car.position[2] });
                    const glm::vec3 turn { car.orientation[0], car.orientation[1], car.orientation[2] };
                    if (const float angle = glm::length(turn); angle > 1e-6f)
                        placed = glm::rotate(placed, angle, turn / angle);
                    out.fitted.emplace(o.id, placed);
                }
            }
        }
        return out;
    }

} // namespace rct3::park
