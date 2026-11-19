// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tracked ride kept as a design, in the game's own format, so the game opens it too: its track as it stands,
// its stations' doors, how it runs, its ratings and test results when it has them, what its track cost and
// a picture of it from above. Named as the game names one, its kind in brackets before its name.
#pragma once

#include "save/model/rides.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rct3::ui
{
    struct InterfaceAssets;
    class StringTable;
} // namespace rct3::ui

namespace rct3::scene::park
{
    struct ParkScene;
}

namespace rct3::scene::track
{

    /// The file name, without its extension, that a design of `ride` is offered: "[Kind]Name", the kind its
    /// symbol names and the name its window shows.
    [[nodiscard]] std::u16string design_file_name(const save::model::Ride& ride, const ui::StringTable& strings);

    /// The design file of `ride` as it stands in the park; empty when it cannot be written (no track, or a
    /// piece that names no asset), which is logged.
    [[nodiscard]] std::vector<std::byte> ride_design_file(park::ParkScene& scene, save::model::RideIndex ride, const ui::InterfaceAssets& assets);

} // namespace rct3::scene::track
