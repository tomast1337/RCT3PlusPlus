// SPDX-License-Identifier: GPL-3.0-or-later
//
// Riding along: the view looks out from a car's camera, a flat ride's seat or fixed camera, or a person's eyes,
// with the ride camera panel stepping from camera to camera, train to train and ride to ride round the park.
// Everything else on screen is put away while riding, and shows again after.
#pragma once

#include "park/object/models.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/park_view/ride_camera.hpp"

#include <cstddef>
#include <vector>

namespace rct3::scene::park
{

    struct ParkScene;

    /// Of the cars of ride `ride`, those with a camera of kind `eye`, in the order the park keeps them.
    [[nodiscard]] std::vector<std::size_t> ride_seats(const ParkScene& scene, std::size_t ride, rct3::park::object::RideEye eye);

    /// Goes with person `person` of `shown_people`, from their eyes or with `behind` from behind them, as
    /// ride_along does in a car; the train buttons go from person to person, the type button changes between
    /// the two views.
    void ride_person(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t person, bool behind);

    /// Rides along on ride `ride` (of the park's rides): a flat ride from its first rider seat; a tracked ride
    /// from the first front seat camera of a train under way, else of the first train, the rear or nose
    /// camera where the cars have no front one. Nothing when it has neither.
    void ride_cam(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride);

    /// Leaves, or changes to the train's first camera of the next kind it has, or steps to the previous or next
    /// camera of the same kind on the train (round from the last to the first), or to the previous or next
    /// train, on from a ride's last train to the next ride's first, round the park.
    void ride_action(ParkScene& scene, const ui::InterfaceAssets& assets, ui::screens::park_view::RideCameraAction action);

    /// Back to the overhead view, with what was on screen before.
    void stop_riding(ParkScene& scene);

} // namespace rct3::scene::park
