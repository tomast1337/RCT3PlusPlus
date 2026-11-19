// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/design.hpp"

#include "core/types.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::save;
using namespace rct3::save::model;

namespace
{
    /// The alpha of pixel `i`, and its grey level.
    u8 alpha(const std::vector<u8>& rgba, usize i)
    {
        return rgba.at(i * 4 + 3);
    }
    u8 level(const std::vector<u8>& rgba, usize i)
    {
        return rgba.at(i * 4);
    }

    /// An invented three-piece circuit with one station: its pieces and platform stand as objects, its doors
    /// beside it.
    struct Fixture
    {
        Ride ride;
        std::vector<PlacedObject> objects;
        DesignSummary summary;
    };

    Fixture invented_ride()
    {
        Fixture f;
        Track track { .facing = 1, .prototype = false, .colours = { 1, 2, 3 }, .segments = { { .first_piece = 0, .piece_count = 3, .closed = true } }, .circuit = true };
        for (i32 i = 0; i < 3; ++i)
        {
            const u64 object = 100 + static_cast<u64>(i);
            f.objects.push_back({ .id = object, .symbol = i == 0 ? "Platform" : "Bend", .overlay = "Tracks/Invented/Track1", .col = 10 + i, .row = 20, .base_height = 3, .owner_ref = object });
            track.pieces.push_back(
                {
                    .id = object,
                    .symbol = i == 0 ? "Platform:tks" : "Bend:tks",
                    .scenery = object,
                    .tile_x = 10 + i,
                    .tile_z = 20,
                    .height = 3,
                    .direction = static_cast<u8>(i),
                    .corner = 2,
                    .reversed = i == 2,
                    .start_distance = { 4.0f * static_cast<f32>(i) },
                    .colours = { 4, 5, 6 },
                    .brake_speed = i == 1 ? std::optional(7) : std::nullopt,
                    .holding_time = 2,
                    .speed_class = i == 1 ? std::optional(9) : std::nullopt,
                    .platform = i == 0 ? std::optional(TrackPlatform { .id = 200, .tile_x = 10, .tile_z = 20, .height = 3, .flags = 8 }) : std::nullopt,
                    .arm_angles = i == 2 ? std::optional(std::array { 1.0f, 2.0f, 3.0f, 4.0f }) : std::nullopt,
                });
        }
        f.objects.push_back({ .id = 201, .symbol = "Ledge", .overlay = "Tracks/Platforms/Invented", .owner_ref = 200 });
        f.objects.push_back({ .id = 300, .symbol = "Way In", .overlay = "Style/Invented/style", .col = 10, .row = 21, .base_height = 3, .rotation = 2, .owner_ref = 300, .colours = { 7, 8, 9 } });
        f.objects.push_back({ .id = 301, .symbol = "Way Out", .overlay = "Style/Invented/style", .col = 11, .row = 21, .base_height = 3, .owner_ref = 301 });
        f.ride = Ride {
            .base = { .symbol = "Invented:trr", .overlay_path = "Tracks/Invented/Ride", .name = "Twister" },
            .mode = OperatingMode::ContinuousCircuit,
            .stations = { { .entrance = 300, .exit = 301 } },
            .options = { .block_sectioned = true, .min_wait = 5, .wait_for_load = 2, .lift_speed = { 6.5f }, .circuits = 2 },
            .car_colours = { 10, 11, 12 },
            .platform_style = "Invented",
            .platform_type = "Plain",
            .track = std::move(track),
            .train_count = 2,
            .cars_per_train = 4,
            .train_selection = 1,
        };
        f.summary = DesignSummary {
            .ride_symbol = "Invented:trr",
            .width = 3,
            .length = 2,
            .cost = { 12345 },
            .assessment = { .valid = true, .excitement = 5.5f, .intensity = 4.25f, .nausea = 2.0f, .value = 30.0f },
            .results = TestResults { .max_speed = 20.0f, .ride_time = { 61.0f }, .drop_count = 3, .inversion_count = 1, .valid = true },
            .scenario = 4,
            .level = 1,
            .train = 1,
            .picture_width = 2,
            .picture_height = 1,
            .picture = { 0, 0, 0, 0, 255, 255, 255, 255 },
        };
        return f;
    }
} // namespace

TEST_CASE("a packed design picture lays opaque pixels and skips clear runs, row after row")
{
    // Invented 3 x 2 picture: clear, full, clear | a third, clear, clear; then the end.
    const std::vector<u8> packed { 64, 63, 64, 21, 0, 9, 65, 0, 0, 63 };
    const auto rgba = design_picture(3, 2, packed, { });
    REQUIRE(rgba.size() == 3 * 2 * 4);
    CHECK(alpha(rgba, 0) == 0);
    CHECK(alpha(rgba, 1) == 255);
    CHECK(level(rgba, 1) == 255);
    CHECK(alpha(rgba, 2) == 0);
    CHECK(alpha(rgba, 3) == 255);
    CHECK(level(rgba, 3) == 85);
    CHECK(alpha(rgba, 4) == 0); // The lone zero took the 9 with it; nothing after the end is drawn
    CHECK(alpha(rgba, 5) == 0);
}

TEST_CASE("an unpacked design picture is clear where its byte is zero")
{
    const std::vector<u8> unpacked { 0, 200, 7, 0 };
    const auto rgba = design_picture(2, 2, { }, unpacked);
    REQUIRE(rgba.size() == 16);
    CHECK(alpha(rgba, 0) == 0);
    CHECK(level(rgba, 1) == 200);
    CHECK(alpha(rgba, 2) == 255);
    CHECK(design_picture(3, 2, { }, unpacked).empty()); // Not the size it claims
    CHECK(design_picture(0, 2, { }, unpacked).empty());
}

TEST_CASE("a picture packed and unpacked again is the same picture")
{
    // One row of 400: a long clear run, more than one skip byte holds, then three levels and clear to the end.
    std::vector<u8> rgba(400 * 4, 0);
    for (const usize at : { 300u, 301u, 302u })
    {
        rgba[at * 4] = rgba[at * 4 + 1] = rgba[at * 4 + 2] = static_cast<u8>(at == 300 ? 255 : at == 301 ? 85 : 1);
        rgba[at * 4 + 3] = 255;
    }
    const auto packed = pack_design_picture(rgba);
    const auto back = design_picture(400, 1, packed, { });
    REQUIRE(back.size() == rgba.size());
    CHECK(alpha(back, 299) == 0);
    CHECK(level(back, 300) == 255);
    CHECK(level(back, 301) == 85);
    CHECK(alpha(back, 302) == 255); // The faintest level still shows
    CHECK(alpha(back, 303) == 0);
}

TEST_CASE("a written design reads back as its summary")
{
    const Fixture f = invented_ride();
    const auto bytes = write_design(f.ride, f.objects, f.summary);
    REQUIRE(bytes);
    const auto s = read_design_summary(*bytes);
    REQUIRE(s);
    CHECK(s->ride_symbol == "Invented:trr");
    CHECK(s->width == 3);
    CHECK(s->length == 2);
    CHECK(s->cost.value == 12345);
    CHECK(s->assessment.valid);
    CHECK(s->assessment.intensity == 4.25f);
    REQUIRE(s->results);
    CHECK(s->results->max_speed == 20.0f);
    CHECK(s->results->inversion_count == 1);
    CHECK(s->scenario == 4);
    CHECK(s->level == 1);
    CHECK(s->train == 1);
    CHECK(s->picture == f.summary.picture);

    // The header is the root, first among the objects.
    const auto file = read_save(*bytes);
    REQUIRE(file);
    CHECK(file->has_header);
    CHECK(file->classes[file->objects.front().class_index].name == "TrackedRideInstanceSaveHeader");
}

TEST_CASE("a written design reads back as the same ride and track")
{
    const Fixture f = invented_ride();
    const auto bytes = write_design(f.ride, f.objects, f.summary);
    REQUIRE(bytes);
    const auto file = read_save(*bytes);
    REQUIRE(file);
    const ObjectIndex index(*file);
    const auto rides = load_rides(*file, index);
    REQUIRE(rides);
    REQUIRE(rides->rides.size() == 1);
    const Ride& r = rides->rides.front();
    CHECK(r.base.name == "Twister");
    CHECK(r.base.symbol == "Invented:trr");
    CHECK(r.base.overlay_path == "Tracks\\Invented\\Ride");
    CHECK(r.options.block_sectioned);
    CHECK(r.options.min_wait == 5);
    CHECK_FALSE(r.options.max_wait);
    CHECK(r.options.wait_for_load == 2);
    CHECK(r.options.lift_speed.value == 6.5f);
    CHECK_FALSE(r.options.fixed_speed);
    CHECK(r.options.circuits == 2);
    CHECK(r.car_colours == RideColours { 10, 11, 12 });
    CHECK(r.platform_type == "Plain");
    CHECK(r.train_count == 2);
    CHECK(r.cars_per_train == 4);
    CHECK(r.train_selection == 1);

    REQUIRE(r.track);
    CHECK(r.track->circuit);
    CHECK(r.track->colours == RideColours { 1, 2, 3 });
    REQUIRE(r.track->pieces.size() == 3);
    for (usize i = 0; i < 3; ++i)
    {
        const TrackPiece& want = f.ride.track->pieces[i];
        const TrackPiece& got = r.track->pieces[i];
        CHECK(got.symbol == want.symbol);
        CHECK(got.tile_x == want.tile_x);
        CHECK(got.direction == want.direction);
        CHECK(got.corner == 2);
        CHECK(got.reversed == want.reversed);
        CHECK(got.start_distance.value == want.start_distance.value);
        CHECK(got.brake_speed == want.brake_speed);
        CHECK(got.speed_class == want.speed_class);
        CHECK(got.arm_angles == want.arm_angles);
        CHECK(got.scenery == 0); // A design's pieces stand nowhere yet
    }
    REQUIRE(r.track->pieces[0].platform);
    CHECK(r.track->pieces[0].platform->flags == 8);

    // The doors, and the assets the pieces name.
    const auto world = load_world(*file, index);
    REQUIRE(world);
    REQUIRE(r.stations.size() == 1);
    const auto door = std::ranges::find(world->objects, r.stations[0].entrance, &PlacedObject::id);
    REQUIRE(door != world->objects.end());
    CHECK(door->symbol == "Way In");
    CHECK(door->rotation == 2);
    CHECK(door->colours == std::array<i32, 3> { 7, 8, 9 });
    const auto asset = index.find(r.track->pieces[1].asset);
    REQUIRE(asset);
    const auto entry = decode_object(*file, *asset);
    REQUIRE(entry);
    CHECK(entry->find("SYMBOLNAME")->text() == "Bend");
    CHECK(entry->find("OVERLAYFILENAME")->text() == "Tracks\\Invented\\Track1");
}

TEST_CASE("a design is refused for a ride with no track or a piece that names no asset")
{
    Fixture f = invented_ride();
    Ride bare = Ride { .base = f.ride.base };
    CHECK(write_design(bare, f.objects, f.summary).error() == DesignError::NoTrack);
    f.objects.erase(f.objects.begin() + 1);
    CHECK(write_design(f.ride, f.objects, f.summary).error() == DesignError::NoAsset);
}
