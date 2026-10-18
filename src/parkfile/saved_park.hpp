// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park the player saved: the original park file it was started from, kept whole, and what the player has
// changed since, as the models stand when saved. Loading reads the original park and puts these over it.
//
// It is a park file (see park_file.hpp) with these chunks, each a layout hash then the value field by field
// (see binary.hpp):
//
//   "BASE" v1  the original park file's bytes
//   "SUMM" v1  save::SaveSummary, for the list of saves
//   "TERR" v1  save::TerrainGrid
//   "PATH" v1  the paths, std::vector<save::PathTile>
//   "OBJS" v1  the placed objects the player keeps, std::vector<save::PlacedObject>: those of the park and
//              those the player built; not the ones made again on loading (supports, railings, cars, trees)
//   "BINS" v1  std::vector<save::BinState>
//   "SIGN" v1  std::vector<save::SignState>
//   "RIDE" v1  save::RideModel
//   "PARK" v1  SavedProgress
//   "VIEW" v1  park::ParkCamera
//
// The guests, staff and the simulation start again from the original park.
#pragma once

#include "park/camera.hpp"
#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/summary.hpp"
#include "simulation/park_rating.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::parkfile
{

    /// Where the park's money and calendar stand.
    struct SavedProgress
    {
        std::optional<std::int64_t> cash; ///< Hundredths; none when money never runs out
        std::int64_t loan = 0;
        std::optional<float> rating;
        std::optional<simulation::RatingParts> rating_parts;
        float time = 0;
        float zero_time = 0;
        save::CalendarDate date;
        save::WeatherType weather = save::WeatherType::Unknown;
        std::optional<save::WeatherType> forecast;
        std::optional<float> temperature;
        float day_time = 0; ///< The day clock's time
    };

    struct SavedPark
    {
        std::vector<std::byte> base;
        save::SaveSummary summary;
        save::TerrainGrid grid;
        std::vector<save::PathTile> paths;
        std::vector<save::PlacedObject> objects;
        std::vector<save::BinState> bins;
        std::vector<save::SignState> signs;
        save::RideModel rides;
        SavedProgress progress;
        park::ParkCamera camera;
    };

    /// The objects a save keeps: the first `own` (the park's own) and those a path or a track piece stands on
    /// (the player's); not those loading makes again (supports, railings, cars, trees).
    [[nodiscard]] std::vector<save::PlacedObject> kept_objects(std::span<const save::PlacedObject> objects, std::size_t own, std::span<const save::PathTile> paths, const save::RideModel& rides);

    [[nodiscard]] std::vector<std::byte> write_saved_park(const SavedPark& park);

    /// The saved park `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes);

    /// Whether `bytes` start as a park file does, rather than an original save.
    [[nodiscard]] bool is_park_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
