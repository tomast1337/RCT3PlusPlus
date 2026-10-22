// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tracked ride's whole track as one line through the park: the middle line between the rails of each
// piece, laid where the piece stands and joined in the order the cars run, starting at the first piece.
// Measured in world units (metres) from that start, so a distance along the track as a park stores it for
// its trains and cars finds a point on the line.
#pragma once

#include "park/object_mesh.hpp"
#include "park/track_pieces.hpp"
#include "save/model/rides.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// What the stretch of track from a point to the next is.
    enum class CourseKind : std::uint8_t
    {
        Plain,
        Chain,   ///< A lift chain pulls the cars up
        Station, ///< Riders get on and off
        Cable,   ///< A cable pulls the cars up, as a chain does
    };

    struct TrackCourse
    {
        std::vector<glm::vec3> points;
        std::vector<glm::vec3> ups;      ///< Up for the track at each point, leaning with its bank; none is straight up
        std::vector<float> distance;     ///< From the start to each point
        std::vector<CourseKind> kinds;   ///< Of the stretch from each point to the next; the last is unused
        std::vector<float> piece_starts; ///< Distance at the start of each of the track's pieces
        bool closed = false;             ///< The end joins the start, so cars go round
        bool reverse_lift = false;       ///< Has the hill a reverse cable lift pulls the train up
        std::size_t first_piece = 0;     ///< Of the track's pieces, the one the course starts on
        [[nodiscard]] float length() const
        {
            return distance.empty() ? 0 : distance.back();
        }
    };

    /// The shape of `piece`, standing as `object`; nothing when not known.
    using PieceShapeOf = std::function<std::optional<const PieceShape&>(const save::TrackPiece& piece, const save::PlacedObject& object)>;

    /// The course of `ride`'s track, its pieces standing as `objects` place them: of a track in several stretches,
    /// the first stretch with a station, or the first stretch when none has one. Nothing when the ride has no
    /// track, or a piece's object or shape is missing.
    [[nodiscard]] std::optional<TrackCourse> track_course(const save::Ride& ride, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const PieceShapeOf& shape_of);

    /// The point `distance` along `course`, going round again past the end of a closed course and held at the
    /// ends of an open one. The course must have a point.
    [[nodiscard]] glm::vec3 course_point(const TrackCourse& course, float distance);

    /// Which way the track runs at `distance`, as course_point finds it; a unit vector.
    [[nodiscard]] glm::vec3 course_direction(const TrackCourse& course, float distance);

    /// Which way is up for the track at `distance`, as course_point finds it; a unit vector.
    [[nodiscard]] glm::vec3 course_up(const TrackCourse& course, float distance);

    /// How long each run of station stretches of `course` is, in the order they start; on a closed course a
    /// run over the end goes on from the start.
    [[nodiscard]] std::vector<float> station_runs(const TrackCourse& course);

    /// Where a car's front and rear wheels stand on a course, as distances along it. A car without wheels of
    /// its own has both at one point.
    struct Axles
    {
        float front = 0;
        float rear = 0;
    };

    /// Where a car the park kept at `saved`, its wheels at `saved_at` along `course`, stands once both are
    /// moved on by `by`: it points from its rear wheels to its front ones, leans with the track's bank
    /// between them, and keeps the place and turn it had against the track.
    [[nodiscard]] glm::mat4 moved_along(const TrackCourse& course, const glm::mat4& saved, Axles saved_at, float by);

    /// Track left clear between trains queued one behind another, in metres.
    inline constexpr float kTrainGap = 2;

    /// How many trains of cars fitted as `fits` (by car) `course` takes: as many as fill half of it queued
    /// kTrainGap apart, so they have room to run (our own rule); at least one.
    [[nodiscard]] int trains_fitting(const TrackCourse& course, std::span<const CarFit> fits);

    /// Stands `train`'s cars nose to tail back along `course` from `front`, each fitted as `fits` has it (by
    /// car; a car past its end fits as the last): its front `length` behind the car before, its wheels on the
    /// track that far behind its front, and its on_track point midway between them, the car pointing from its
    /// rear wheels to its front ones. Sets each car's distance (its front), length, wheels and place and turn
    /// as the park keeps them, and the train's distance (its front) and length. Nothing without fits.
    void stand_train(const TrackCourse& course, save::Train& train, std::span<const CarFit> fits, float front);

    using ArmAngles = std::array<float, 4>; ///< An arm's joint angles, degrees, from its base out

    /// The arm angles of a car `distance` along `course`, whose pieces hold `held` (by piece) at their middles.
    /// Between the middles of a piece and the one before or after, each joint turns evenly from one's angle to
    /// the other's, round the end of a closed course; where only one of the two holds angles the car takes
    /// those. Nothing where neither does.
    [[nodiscard]] std::optional<ArmAngles> arm_angles_at(const TrackCourse& course, std::span<const std::optional<ArmAngles>> held, float distance);

} // namespace rct3::park
