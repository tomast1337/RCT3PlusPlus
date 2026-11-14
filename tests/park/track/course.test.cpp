// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/course.hpp"

#include <algorithm>
#include <array>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;

namespace
{
    /// 10 along x, then 10 along z.
    park::track::TrackCourse corner()
    {
        park::track::TrackCourse c;
        c.points = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 } };
        c.distance = { 0, 10, 20 };
        c.kinds = { park::track::CourseKind::Plain, park::track::CourseKind::Plain, park::track::CourseKind::Plain };
        return c;
    }

    void check_near(const glm::vec3& a, const glm::vec3& b)
    {
        CHECK(a.x == doctest::Approx(b.x));
        CHECK(a.y == doctest::Approx(b.y));
        CHECK(a.z == doctest::Approx(b.z));
    }
} // namespace

TEST_CASE("a course finds points along its length, and round again when closed")
{
    park::track::TrackCourse c = corner();
    check_near(park::track::course_point(c, 15), { 10, 0, 5 });
    check_near(park::track::course_direction(c, 15), { 0, 0, 1 });
    check_near(park::track::course_point(c, 25), { 10, 0, 10 }); // Held at the end of an open course
    c.closed = true;
    check_near(park::track::course_point(c, 25), { 5, 0, 0 });
}

TEST_CASE("a car moved along a course keeps its place off the track and turns with it")
{
    const park::track::TrackCourse c = corner();
    // Kept 2 above the track with its wheels at 4 and 6 along, facing +x (its model faces +z).
    const glm::mat4 saved = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 2, 0 }), glm::radians(90.0f), { 0, 1, 0 });
    check_near(glm::vec3(park::track::moved_along(c, saved, { 6, 4 }, 0)[3]), { 5, 2, 0 });
    const glm::mat4 moved = park::track::moved_along(c, saved, { 6, 4 }, 10);
    check_near(glm::vec3(moved[3]), { 10, 2, 5 });
    check_near(glm::vec3(moved * glm::vec4(0, 0, 1, 0)), { 0, 0, 1 }); // Its front now faces along z
    check_near(glm::vec3(moved * glm::vec4(0, 1, 0, 0)), { 0, 1, 0 });
}

TEST_CASE("a train stands nose to tail back from its front, its wheels on the track")
{
    const park::track::TrackCourse c = corner();
    save::model::Train train;
    train.cars.resize(3);
    // A car 2 long, its wheels 0.5 and 1.5 behind its front and 1 below its middle; the last fits as the second.
    const std::array fits { park::object::CarFit { .length = 2, .front_wheels = 0.5f, .rear_wheels = 1.5f, .on_track = { 0, -1, 0 } }, park::object::CarFit { .length = 3, .front_wheels = 1, .rear_wheels = 2, .on_track = { 0, 0, -0.5f } } };
    park::track::stand_train(c, train, fits, 8);
    CHECK(train.distance == 8);
    CHECK(train.length == 8);
    const save::model::Car& first = train.cars[0];
    CHECK(first.distance == 8);
    CHECK(first.length == 2);
    CHECK(first.front_wheel_distance == doctest::Approx(7.5f));
    CHECK(first.rear_wheel_distance == doctest::Approx(6.5f));
    CHECK(first.position_valid);
    // Raised so the point between its wheels is on the track, which is at 7 along.
    check_near({ first.position[0], first.position[1], first.position[2] }, { 7, 1, 0 });
    // Turned from its model, which faces +z, to face +x along the track.
    const glm::vec3 axis { first.orientation[0], first.orientation[1], first.orientation[2] };
    check_near(glm::vec3(glm::rotate(glm::mat4(1.0f), glm::length(axis), glm::normalize(axis)) * glm::vec4(0, 0, 1, 0)), { 1, 0, 0 });
    // The second's front 2 behind, its wheels at 5 and 4 along, its point between them 0.5 behind its origin.
    CHECK(train.cars[1].distance == 6);
    check_near({ train.cars[1].position[0], train.cars[1].position[1], train.cars[1].position[2] }, { 5, 0, 0 });
    CHECK(train.cars[2].distance == 3);
}

TEST_CASE("a link couples a train's cars, reaching along the track from where one couples on to where the next does")
{
    const park::track::TrackCourse c = corner();
    save::model::Train train;
    train.cars.resize(3);
    // Cars 2 long, coupling on 0.25 behind their front and 0.25 ahead of their rear, 0.5 over their wheels; a link
    // reaching 1 between them.
    const park::object::CarFit car { .length = 2, .front_wheels = 0.5f, .rear_wheels = 1.5f, .on_track = { 0, 0, 1 }, .link_front = 0.25f, .link_rear = 1.75f, .link_height = 0.5f, .link = false };
    const park::object::CarFit link { .length = 1, .front_wheels = 0, .rear_wheels = 1, .on_track = { 0, 0, 0 }, .link_front = 0, .link_rear = 1, .link_height = 0, .link = true };
    const std::array fits { car, link, car };
    park::track::stand_train(c, train, fits, 8);
    // The link from 6.25 to 5.25 along, so the car behind's front is 0.25 further on, at 5.5.
    CHECK(train.cars[1].distance == 6);
    CHECK(train.cars[1].length == doctest::Approx(0.5f));
    CHECK(train.cars[1].front_wheel_distance == doctest::Approx(6.25f));
    CHECK(train.cars[1].rear_wheel_distance == doctest::Approx(5.25f));
    check_near({ train.cars[1].position[0], train.cars[1].position[1], train.cars[1].position[2] }, { 5.75f, 0.5f, 0 });
    CHECK(train.cars[2].distance == doctest::Approx(5.5f));
    CHECK(train.length == doctest::Approx(4.5f));
    CHECK(park::track::train_length(fits) == doctest::Approx(4.5f));
}

TEST_CASE("a car points from its rear wheels to its front ones, and leans with the track")
{
    // The corner, then on along z, banked to the right from the corner on.
    park::track::TrackCourse c = corner();
    c.points.push_back({ 10, 0, 20 });
    c.distance.push_back(30);
    c.kinds.push_back(park::track::CourseKind::Plain);
    const glm::vec3 leaning = glm::normalize(glm::vec3 { 1, 1, 0 });
    c.ups = { { 0, 1, 0 }, { 0, 1, 0 }, leaning, leaning };
    check_near(park::track::course_up(c, 25), leaning);
    // Kept flat on the track with its wheels at 4 and 6 along, facing +x.
    const glm::mat4 saved = glm::rotate(glm::translate(glm::mat4(1.0f), { 5, 0, 0 }), glm::radians(90.0f), { 0, 1, 0 });
    // Straddling the corner, it stands on the line between its wheels rather than on the bend.
    const glm::mat4 straddling = park::track::moved_along(c, saved, { 6, 4 }, 5);
    check_near(glm::vec3(straddling[3]), { 9.5f, 0, 0.5f });
    check_near(glm::vec3(straddling * glm::vec4(0, 0, 1, 0)), glm::normalize(glm::vec3 { 1, 0, 1 }));
    // Both wheels on the banked stretch: it leans as the track does.
    const glm::mat4 banked = park::track::moved_along(c, saved, { 6, 4 }, 16);
    check_near(glm::vec3(banked[3]), { 10, 0, 11 });
    check_near(glm::vec3(banked * glm::vec4(0, 1, 0, 0)), leaning);
    check_near(glm::vec3(banked * glm::vec4(0, 0, 1, 0)), { 0, 0, 1 });
}

TEST_CASE("station runs are measured from one end of the station to the other, joined over a closed course's end")
{
    using K = park::track::CourseKind;
    park::track::TrackCourse c;
    c.points.resize(6);
    c.distance = { 0, 4, 10, 12, 15, 20 };
    c.kinds = { K::Station, K::Plain, K::Station, K::Station, K::Plain, K::Plain };
    CHECK(park::track::station_runs(c) == std::vector<float> { 4, 5 });
    c.closed = true;
    CHECK(park::track::station_runs(c) == std::vector<float> { 4, 5 });
    // The last stretch is a station too: on a closed course it runs on into the first.
    c.kinds = { K::Station, K::Plain, K::Plain, K::Plain, K::Station, K::Plain };
    CHECK(park::track::station_runs(c) == std::vector<float> { 9 });
    c.closed = false;
    CHECK(park::track::station_runs(c) == std::vector<float> { 4, 5 });
    CHECK(park::track::station_runs(corner()).empty());
}

TEST_CASE("an arm holds each piece's angles at its middle and turns evenly between them")
{
    park::track::TrackCourse c;
    c.distance = { 0, 40 };
    c.points = { { 0, 0, 0 }, { 40, 0, 0 } };
    c.piece_starts = { 0, 10, 20, 30 }; // Middles at 5, 15, 25 and 35
    c.closed = true;
    using A = park::track::ArmAngles;
    const std::vector<std::optional<A>> held { A { 0, 0, 0, 0 }, A { 100, 10, -20, 40 }, std::nullopt, A { 20, 0, 0, 0 } };
    CHECK(park::track::arm_angles_at(c, held, 15) == A { 100, 10, -20, 40 });
    CHECK(park::track::arm_angles_at(c, held, 10) == A { 50, 5, -10, 20 });
    // Beside a piece that holds none, the car takes the angles of the one that does.
    CHECK(park::track::arm_angles_at(c, held, 22) == A { 100, 10, -20, 40 });
    CHECK(park::track::arm_angles_at(c, held, 28) == A { 20, 0, 0, 0 });
    // Round the end of the circuit, from the last middle to the first.
    CHECK(park::track::arm_angles_at(c, held, 0) == A { 10, 0, 0, 0 });
    CHECK(park::track::arm_angles_at(c, held, 40) == A { 10, 0, 0, 0 });
    CHECK_FALSE(park::track::arm_angles_at(c, std::vector<std::optional<A>>(4), 12));
}

TEST_CASE("as many trains run as fill half the course with gaps between them, within its kind's and its blocks' limits, and always one")
{
    park::track::TrackCourse c = corner();
    c.points.push_back({ 0, 0, 10 });
    c.distance.push_back(100);
    c.kinds.push_back(park::track::CourseKind::Plain);
    // Trains of two cars 3 and 5 long take 8, and 10 with the gap after them: five fill the 50 of half the course.
    const std::array fits { park::object::CarFit { .length = 3 }, park::object::CarFit { .length = 5 } };
    CHECK(park::track::trains_fitting(c, fits, false, std::nullopt) == 5);
    CHECK(park::track::trains_fitting(corner(), fits, false, std::nullopt) == 1);
    // Its kind may set fewer.
    CHECK(park::track::trains_fitting(c, fits, false, 3u) == 3);
    // Kept a block apart, one fewer than its blocks: a station and a block brake make two.
    c.kinds = { park::track::CourseKind::Station, park::track::CourseKind::Plain, park::track::CourseKind::BlockBrake, park::track::CourseKind::Plain };
    CHECK(park::track::trains_fitting(c, fits, true, std::nullopt) == 1);
    c.kinds = { park::track::CourseKind::Station, park::track::CourseKind::Chain, park::track::CourseKind::BlockBrake, park::track::CourseKind::Plain };
    CHECK(park::track::trains_fitting(c, fits, true, std::nullopt) == 2);
}

TEST_CASE("several trains stand at the stations first, then queued behind them or a block apart")
{
    // A closed 100 m course: a station ending at 10, a lift ending at 40, a second station ending at 60.
    park::track::TrackCourse c;
    for (int i = 0; i <= 10; ++i)
    {
        c.points.push_back({ static_cast<float>(i) * 10, 0, 0 });
        c.distance.push_back(static_cast<float>(i) * 10);
        c.kinds.push_back(park::track::CourseKind::Plain);
    }
    c.closed = true;
    c.kinds[0] = park::track::CourseKind::Station;
    c.kinds[3] = park::track::CourseKind::Chain;
    c.kinds[5] = park::track::CourseKind::Station;
    // One at each station from the first; then queued 8 + 2 behind each in turn.
    CHECK(park::track::train_fronts(c, 10, 8, 2, false) == std::vector<float> { 10, 60 });
    CHECK(park::track::train_fronts(c, 10, 8, 4, false) == std::vector<float> { 10, 60, 0, 50 });
    // Behind the second station only 20 m to the lift top: room for two. Behind the first, 50 to the second: five.
    // The lift top then takes what is left, back to the first station's end.
    CHECK(park::track::train_fronts(c, 10, 8, 9, false).size() == 9);
    CHECK(park::track::train_fronts(c, 10, 8, 20, false).size() == 2 + 5 + 3);
    // A block apart, one at each block end: the stations, then the lift top.
    CHECK(park::track::train_fronts(c, 10, 8, 5, true) == std::vector<float> { 10, 60, 40 });
    // Starting from the second station puts the first train there.
    CHECK(park::track::train_fronts(c, 60, 8, 2, false) == std::vector<float> { 60, 10 });
}

TEST_CASE("a train has as many cars as fit along its shortest station, within what its kind allows")
{
    park::track::TrackCourse c = corner();
    c.kinds = { park::track::CourseKind::Station, park::track::CourseKind::Plain, park::track::CourseKind::Plain };
    // A front car 3 long and the others 2 each: four take 9 of the station's 10.
    const std::array lengths { 3.0f, 5.0f, 7.0f, 9.0f, 11.0f, 13.0f };
    CHECK(park::track::cars_fitting(c, lengths, 1) == 4);
    CHECK(park::track::cars_fitting(c, std::span(lengths).first(3), 1) == 3);
    CHECK(park::track::cars_fitting(c, lengths, 5) == 0);
    c.kinds[0] = park::track::CourseKind::Plain;
    CHECK(park::track::cars_fitting(c, lengths, 1) == 6);
}

TEST_CASE("a track in several stretches runs along the first one with a station")
{
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 8;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(64);
    park::track::PieceShape plain;
    plain.line.path = { { 2, 0, 0 }, { -2, 0, 0 } };
    park::track::PieceShape station = plain;
    station.use = park::track::PieceUse::Station;
    save::model::Ride ride;
    auto& track = ride.track.emplace();
    std::vector<save::model::PlacedObject> objects;
    // Two plain pieces, then a stretch of a station and a plain piece that closes on itself.
    for (std::uint64_t i = 0; i < 4; ++i)
    {
        save::model::PlacedObject& o = objects.emplace_back();
        o.id = 100 + i;
        o.col = static_cast<int>(i);
        o.force_absolute_height = true;
        save::model::TrackPiece& piece = track.pieces.emplace_back();
        piece.symbol = i == 2 ? "Station" : "Plain";
        piece.scenery = o.id;
    }
    track.segments = { { .first_piece = 0, .piece_count = 2 }, { .first_piece = 2, .piece_count = 2, .closed = true } };
    const auto shape_of = [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) -> std::optional<const park::track::PieceShape&> { return p.symbol == "Station" ? station : plain; };
    const auto course = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
    REQUIRE(course);
    CHECK(course->first_piece == 2);
    CHECK(course->piece_starts.size() == 2);
    CHECK(course->closed);
    CHECK(course->kinds.front() == park::track::CourseKind::Station);
    // Without a station anywhere, the first stretch.
    track.pieces[2].symbol = "Plain";
    const auto bare = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
    REQUIRE(bare);
    CHECK(bare->first_piece == 0);
    CHECK(!bare->closed);
}

TEST_CASE("holding pieces end blocks, lift tops are lift, and a tower's chain-marked pieces are no lift")
{
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 8;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(64);
    park::track::PieceShape plain;
    plain.line.path = { { 2, 0, 0 }, { -2, 0, 0 } };
    park::track::PieceShape holds = plain;
    holds.record.flags = park::track::kPieceHolds;
    park::track::PieceShape top = plain;
    top.record.flags = park::track::kPieceLiftTop;
    park::track::PieceShape tower = plain;
    tower.toggles = park::track::toggle_bit(park::track::PieceToggle::Chain);
    tower.record.role = park::track::kRoleTowerFirst;
    save::model::Ride ride;
    auto& track = ride.track.emplace();
    std::vector<save::model::PlacedObject> objects;
    const std::array<std::string, 4> symbols { "Holds", "Top", "Tower", "Plain" };
    for (std::uint64_t i = 0; i < symbols.size(); ++i)
    {
        save::model::PlacedObject& o = objects.emplace_back();
        o.id = 100 + i;
        o.col = static_cast<int>(i);
        o.force_absolute_height = true;
        save::model::TrackPiece& piece = track.pieces.emplace_back();
        piece.symbol = symbols[i];
        piece.scenery = o.id;
    }
    track.segments = { { .first_piece = 0, .piece_count = 4 } };
    const auto shape_of = [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) -> std::optional<const park::track::PieceShape&> { return p.symbol == "Holds" ? holds : p.symbol == "Top" ? top : p.symbol == "Tower" ? tower : plain; };
    const auto course = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
    REQUIRE(course);
    REQUIRE(course->piece_starts.size() == 4);
    const auto kind_of = [&](std::size_t piece) {
        const auto after = std::ranges::upper_bound(course->distance, course->piece_starts[piece] + 0.01f);
        return course->kinds[static_cast<std::size_t>(after - course->distance.begin() - 1)];
    };
    CHECK(kind_of(0) == park::track::CourseKind::BlockBrake);
    CHECK(kind_of(1) == park::track::CourseKind::Lift);
    CHECK(kind_of(2) == park::track::CourseKind::Plain);
}

TEST_CASE("a lift tower's foot, middle and top make one lift run, its foot keeping the start of the car's rails")
{
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 8;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(64);
    park::track::PieceShape plain;
    plain.line.path = { { 2, 0, 0 }, { -2, 0, 0 } };
    // The foot's rails start 3 m up, above the far end of the track before it, and climb 2 m more.
    park::track::PieceShape foot;
    foot.line.path = { { -2, 3, 0 }, { -2, 5, 0 } };
    foot.record.flags = park::track::kPieceLiftBottom;
    park::track::PieceShape middle;
    middle.line.path = { { -2, 5, 0 }, { -2, 9, 0 } };
    middle.record.flags = park::track::kPieceLiftMiddle;
    park::track::PieceShape top = middle;
    top.line.path = { { -2, 9, 0 }, { -2, 13, 0 } };
    top.record.flags = park::track::kPieceLiftTop;
    save::model::Ride ride;
    auto& track = ride.track.emplace();
    std::vector<save::model::PlacedObject> objects;
    const std::array<std::string, 5> symbols { "Plain", "Foot", "Middle", "Top", "Plain" };
    for (std::uint64_t i = 0; i < symbols.size(); ++i)
    {
        save::model::PlacedObject& o = objects.emplace_back();
        o.id = 100 + i;
        o.col = 0;
        o.force_absolute_height = true;
        save::model::TrackPiece& piece = track.pieces.emplace_back();
        piece.symbol = symbols[i];
        piece.scenery = o.id;
    }
    track.segments = { { .first_piece = 0, .piece_count = 5 } };
    const auto shape_of = [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) -> std::optional<const park::track::PieceShape&> { return p.symbol == "Foot" ? foot : p.symbol == "Middle" ? middle : p.symbol == "Top" ? top : plain; };
    const auto course = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
    REQUIRE(course);
    REQUIRE(course->lifts.size() == 1);
    const park::track::LiftRun& run = course->lifts.front();
    CHECK(run.entry == doctest::Approx(4));
    CHECK(run.from == doctest::Approx(7));
    CHECK(run.to == doctest::Approx(17));
    CHECK(course->kinds[1] == park::track::CourseKind::Lift);
}

TEST_CASE("brakes and block brakes keep their own speed, 0 when they have none, and other pieces none at all")
{
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 8;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(64);
    park::track::PieceShape plain;
    plain.line.path = { { 2, 0, 0 }, { -2, 0, 0 } };
    park::track::PieceShape brakes = plain;
    brakes.record.role = park::track::kRoleBrakes;
    park::track::PieceShape block = plain;
    block.record.role = park::track::kRoleBlockBrake;
    park::track::PieceShape holds = plain;
    holds.record.flags = park::track::kPieceHolds;
    save::model::Ride ride;
    auto& track = ride.track.emplace();
    std::vector<save::model::PlacedObject> objects;
    const std::array<std::string, 4> symbols { "Brakes", "Block", "Holds", "Plain" };
    const std::array<std::optional<std::int32_t>, 4> speeds { 6, std::nullopt, std::nullopt, 9 };
    for (std::uint64_t i = 0; i < symbols.size(); ++i)
    {
        save::model::PlacedObject& o = objects.emplace_back();
        o.id = 100 + i;
        o.col = static_cast<int>(i);
        o.force_absolute_height = true;
        save::model::TrackPiece& piece = track.pieces.emplace_back();
        piece.symbol = symbols[i];
        piece.scenery = o.id;
        piece.brake_speed = speeds[i];
    }
    track.segments = { { .first_piece = 0, .piece_count = 4 } };
    const auto shape_of = [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) -> std::optional<const park::track::PieceShape&> { return p.symbol == "Brakes" ? brakes : p.symbol == "Block" ? block : p.symbol == "Holds" ? holds : plain; };
    const auto course = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
    REQUIRE(course);
    REQUIRE(course->piece_starts.size() == 4);
    const auto speed_of = [&](std::size_t piece) {
        const auto after = std::ranges::upper_bound(course->distance, course->piece_starts[piece] + 0.01f);
        return course->brake_speeds[static_cast<std::size_t>(after - course->distance.begin() - 1)];
    };
    CHECK(speed_of(0) == 6);
    CHECK(speed_of(1) == 0);
    CHECK(speed_of(2) < 0);
    CHECK(speed_of(3) < 0);
}

TEST_CASE("a pool slide's track ends open at a bowl, a funnel or an end piece, and any other last piece is no slide end")
{
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 8;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(64);
    park::track::PieceShape plain;
    plain.line.path = { { 2, 0, 0 }, { -2, 0, 0 } };
    const auto ends_open = [&](std::uint32_t role) {
        park::track::PieceShape last = plain;
        last.record.role = role;
        save::model::Ride ride;
        auto& track = ride.track.emplace();
        std::vector<save::model::PlacedObject> objects;
        for (std::uint64_t i = 0; i < 2; ++i)
        {
            save::model::PlacedObject& o = objects.emplace_back();
            o.id = 100 + i;
            o.col = static_cast<int>(i);
            o.force_absolute_height = true;
            save::model::TrackPiece& piece = track.pieces.emplace_back();
            piece.symbol = i == 0 ? "Plain" : "Last";
            piece.scenery = o.id;
        }
        track.segments = { { .first_piece = 0, .piece_count = 2 } };
        const auto shape_of = [&](const save::model::TrackPiece& p, const save::model::PlacedObject&) -> std::optional<const park::track::PieceShape&> { return p.symbol == "Last" ? last : plain; };
        const auto course = park::track::track_course(ride, objects, { }, grid, { }, shape_of);
        REQUIRE(course);
        return course->splash_end;
    };
    CHECK(ends_open(park::track::kRoleSlideBowl));
    CHECK(ends_open(park::track::kRoleSlideFunnel));
    CHECK(ends_open(park::track::kRoleSlideEnd));
    CHECK_FALSE(ends_open(0));
    CHECK_FALSE(ends_open(park::track::kRoleBrakes));
}
