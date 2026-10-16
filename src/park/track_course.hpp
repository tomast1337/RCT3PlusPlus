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
    };

    struct TrackCourse
    {
        std::vector<glm::vec3> points;
        std::vector<float> distance;     ///< From the start to each point
        std::vector<CourseKind> kinds;   ///< Of the stretch from each point to the next; the last is unused
        std::vector<float> piece_starts; ///< Distance at the start of each of the track's pieces
        bool closed = false;             ///< The end joins the start, so cars go round
        [[nodiscard]] float length() const
        {
            return distance.empty() ? 0 : distance.back();
        }
    };

    /// The shape of `piece`, standing as `object`; nothing when not known.
    using PieceShapeOf = std::function<const PieceShape*(const save::TrackPiece& piece, const save::PlacedObject& object)>;

    /// The course of `ride`'s track, its pieces standing as `objects` place them. Nothing when the ride has no
    /// track, or a piece's object or shape is missing.
    [[nodiscard]] std::optional<TrackCourse> track_course(const save::Ride& ride, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const PieceShapeOf& shape_of);

    /// The point `distance` along `course`, going round again past the end of a closed course and held at the
    /// ends of an open one. The course must have a point.
    [[nodiscard]] glm::vec3 course_point(const TrackCourse& course, float distance);

    /// Which way the track runs at `distance`, as course_point finds it; a unit vector.
    [[nodiscard]] glm::vec3 course_direction(const TrackCourse& course, float distance);

    /// Where a car the park kept at `saved`, `saved_distance` along `course`, stands once moved to `distance`:
    /// as far off the course there, and turned as much as the track turns between the two.
    /// shortcut: the turn is the least that takes one direction to the other, so a car keeps its saved roll,
    /// upgrade when the course knows the track's bank.
    [[nodiscard]] glm::mat4 moved_along(const TrackCourse& course, const glm::mat4& saved, float saved_distance, float distance);

} // namespace rct3::park
