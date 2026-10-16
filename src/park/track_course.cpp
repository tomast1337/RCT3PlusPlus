// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_course.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {
        /// The stretch `distance` falls in, and the distance taken back inside the course.
        std::pair<std::size_t, float> stretch_at(const TrackCourse& course, float distance)
        {
            const float length = course.length();
            if (course.closed && length > 0)
                distance = std::fmod(std::fmod(distance, length) + length, length);
            distance = std::clamp(distance, 0.0f, length);
            const auto after = std::ranges::upper_bound(course.distance, distance);
            const auto i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(after - course.distance.begin() - 1, 0));
            return { std::min(i, course.points.size() - 1), distance };
        }
    } // namespace

    std::optional<TrackCourse> track_course(const save::Ride& ride, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const PieceShapeOf& shape_of)
    {
        if (!ride.track || ride.track->pieces.empty())
            return std::nullopt;
        std::unordered_map<std::uint64_t, const save::PlacedObject*> by_id;
        for (const save::PlacedObject& o : objects)
            by_id.emplace(o.id, &o);
        TrackCourse course;
        for (const save::TrackPiece& piece : ride.track->pieces)
        {
            const auto found = by_id.find(piece.scenery);
            if (found == by_id.end())
                return std::nullopt;
            const save::PlacedObject& object = *found->second;
            const PieceShape* shape = shape_of(piece, object);
            if (shape == nullptr || shape->line.path.size() < 2)
                return std::nullopt;
            const auto model = models.find(model_key(object));
            const glm::mat4 to_world = placed_transform(object, model != models.end() ? &model->second : nullptr, grid, fitted);
            const CourseKind kind = shape->use == PieceUse::Station ? CourseKind::Station : shape->chain ? CourseKind::Chain : CourseKind::Plain;
            std::vector<glm::vec3> path = shape->line.path;
            if (piece.reversed)
                std::ranges::reverse(path);
            // Each piece starts where the last one ended; that point is taken once, and runs into this piece.
            if (!course.kinds.empty())
                course.kinds.back() = kind;
            course.piece_starts.push_back(course.length());
            for (std::size_t k = course.points.empty() ? 0 : 1; k < path.size(); ++k)
            {
                const glm::vec3 at = glm::vec3(to_world * glm::vec4(path[k], 1.0f));
                course.distance.push_back(course.points.empty() ? 0 : course.distance.back() + glm::distance(course.points.back(), at));
                course.points.push_back(at);
                course.kinds.push_back(kind);
            }
        }
        course.closed = ride.track->circuit;
        return course;
    }

    glm::vec3 course_point(const TrackCourse& course, float distance)
    {
        const auto [i, d] = stretch_at(course, distance);
        if (i + 1 >= course.points.size())
            return course.points[i];
        const float span = course.distance[i + 1] - course.distance[i];
        return glm::mix(course.points[i], course.points[i + 1], span > 0 ? (d - course.distance[i]) / span : 0.0f);
    }

    glm::vec3 course_direction(const TrackCourse& course, float distance)
    {
        const auto [i, d] = stretch_at(course, distance);
        const std::size_t a = std::min(i, course.points.size() - 2);
        const glm::vec3 way = course.points[a + 1] - course.points[a];
        const float l = glm::length(way);
        return l > 0 ? way / l : glm::vec3 { 1, 0, 0 };
    }

    glm::mat4 moved_along(const TrackCourse& course, const glm::mat4& saved, float saved_distance, float distance)
    {
        const glm::vec3 from = course_direction(course, saved_distance);
        const glm::vec3 to = course_direction(course, distance);
        const glm::vec3 axis = glm::cross(from, to);
        const float angle = std::atan2(glm::length(axis), glm::dot(from, to));
        glm::mat4 turn(1.0f);
        if (glm::length(axis) > 1e-6f)
            turn = glm::rotate(turn, angle, glm::normalize(axis));
        else if (angle > 1.0f) // Turned right round: about any axis square to the way
            turn = glm::rotate(turn, angle, glm::normalize(glm::abs(from.y) < 0.9f ? glm::cross(from, glm::vec3(0, 1, 0)) : glm::cross(from, glm::vec3(1, 0, 0))));
        glm::mat4 moved = turn * saved;
        const glm::vec3 off = glm::vec3(saved[3]) - course_point(course, saved_distance);
        moved[3] = glm::vec4(course_point(course, distance) + glm::vec3(turn * glm::vec4(off, 0.0f)), 1.0f);
        return moved;
    }

} // namespace rct3::park
