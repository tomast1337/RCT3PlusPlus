// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track/design_file.hpp"

#include "logging/log.hpp"
#include "park/track/top_view.hpp"
#include "save/model/design.hpp"
#include "scene/park/scene.hpp"
#include "scene/track/building.hpp"
#include "strings/unicode.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/info/attraction.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace rct3::scene::track
{

    std::u16string design_file_name(const save::model::Ride& ride, const ui::StringTable& strings)
    {
        const std::string_view symbol = ride.base.symbol;
        std::u16string name = u"[" + strings::to_utf16(symbol.substr(0, symbol.find(':'))) + u"]" + ui::screens::info::attraction_name(ride.base, strings);
        // A name may hold what a file name may not.
        std::ranges::replace_if(name, [](char16_t c) { return std::u16string_view(u"/\\:*?\"<>|").contains(c); }, u'_');
        return name;
    }

    std::vector<std::byte> ride_design_file(park::ParkScene& scene, save::model::RideIndex index, const ui::InterfaceAssets& assets)
    {
        save::model::Ride ride = scene.rides.rides.at(index.value);
        ride.base.name = strings::to_utf8(ui::screens::info::attraction_name(ride.base, *assets.strings));
        const auto& course = index.value < scene.courses.size() ? scene.courses[index.value] : std::nullopt;
        // Each piece's distance from the start, as the course measures it.
        // shortcut: on a track of several stretches the course runs along the first only, so the others' pieces
        // keep no distance; measure each stretch once the course does.
        if (course && ride.track)
            for (std::size_t k = 0; k < course->piece_starts.size() && course->first_piece + k < ride.track->pieces.size(); ++k)
                ride.track->pieces[course->first_piece + k].start_distance = units::Distance { course->piece_starts[k] };

        // The tiles it covers across and along.
        // shortcut: from the tiles its pieces and doors stand on, not every tile a long piece reaches over; count
        // those once a piece's footprint is known here.
        std::int32_t low_col = std::numeric_limits<std::int32_t>::max(), high_col = std::numeric_limits<std::int32_t>::min();
        std::int32_t low_row = low_col, high_row = high_col;
        const auto cover = [&](std::int32_t col, std::int32_t row) {
            low_col = std::min(low_col, col);
            high_col = std::max(high_col, col);
            low_row = std::min(low_row, row);
            high_row = std::max(high_row, row);
        };
        for (const save::model::TrackPiece& piece : ride.track ? ride.track->pieces : std::vector<save::model::TrackPiece> { })
            cover(piece.tile_x, piece.tile_z);
        for (const save::model::Station& station : ride.stations)
            for (const auto& door : { station.entrance_spot, station.exit_spot })
                if (door)
                    cover(door->col, door->row);
        const bool covers = low_col <= high_col;

        std::vector<std::uint8_t> picture = course ? rct3::park::track::top_view(course->points) : std::vector<std::uint8_t> { };
        const std::uint32_t side = picture.empty() ? 0 : rct3::park::track::kTopViewSize;
        const save::model::DesignSummary summary {
            .ride_symbol = ride.base.symbol,
            .width = covers ? high_col - low_col + 1 : 0,
            .length = covers ? high_row - low_row + 1 : 0,
            .cost = track_cost(scene, index, assets),
            .assessment = ride.assessment,
            .results = ride.stats.last_lap && ride.stats.last_lap->valid ? ride.stats.last_lap : std::nullopt,
            .train = ride.train_selection,
            .picture_width = side,
            .picture_height = side,
            .picture = std::move(picture),
        };
        auto bytes = save::model::write_design(ride, scene.objects, summary);
        if (!bytes)
        {
            logging::logger("park").warning("design of {}: {}", ride.base.name, save::model::to_string(bytes.error()));
            return { };
        }
        return std::move(*bytes);
    }

} // namespace rct3::scene::track
