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
            const auto shape = shape_of(piece, object);
            if (!shape || shape->line.path.size() < 2)
                return std::nullopt;
            const auto model = models.find(model_key(object));
            const glm::mat4 to_world = placed_transform(object, model != models.end() ? &model->second : nullptr, grid, fitted);
            const CourseKind kind = shape->use == PieceUse::Station ? CourseKind::Station : shape->chain ? CourseKind::Chain : CourseKind::Plain;
            std::vector<glm::vec3> path = shape->line.path;
            std::vector<glm::vec3> ups = shape->line.ups;
            ups.resize(path.size(), glm::vec3 { 0, 1, 0 });
            if (piece.reversed)
            {
                std::ranges::reverse(path);
                std::ranges::reverse(ups); // Up stays up: the way and the rails both turn round
            }
            // Each piece starts where the last one ended; that point is taken once, and runs into this piece.
            if (!course.kinds.empty())
                course.kinds.back() = kind;
            course.piece_starts.push_back(course.length());
            for (std::size_t k = course.points.empty() ? 0 : 1; k < path.size(); ++k)
            {
                const glm::vec3 at = glm::vec3(to_world * glm::vec4(path[k], 1.0f));
                course.distance.push_back(course.points.empty() ? 0 : course.distance.back() + glm::distance(course.points.back(), at));
                course.points.push_back(at);
                course.ups.push_back(glm::normalize(glm::mat3(to_world) * ups[k]));
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
        const auto [i, _] = stretch_at(course, distance);
        const std::size_t a = std::min(i, course.points.size() - 2);
        const glm::vec3 way = course.points[a + 1] - course.points[a];
        const float l = glm::length(way);
        return l > 0 ? way / l : glm::vec3 { 1, 0, 0 };
    }

    glm::vec3 course_up(const TrackCourse& course, float distance)
    {
        if (course.ups.size() != course.points.size())
            return { 0, 1, 0 };
        const auto [i, d] = stretch_at(course, distance);
        if (i + 1 >= course.points.size())
            return course.ups[i];
        const float span = course.distance[i + 1] - course.distance[i];
        const glm::vec3 up = glm::mix(course.ups[i], course.ups[i + 1], span > 0 ? (d - course.distance[i]) / span : 0.0f);
        const float l = glm::length(up);
        return l > 1e-6f ? up / l : course.ups[i];
    }

    namespace
    {
        /// The track's own frame under a car with wheels `at`: x across, y up, z from the rear wheels to the
        /// front ones, standing midway between them.
        glm::mat4 frame_at(const TrackCourse& course, Axles at)
        {
            const glm::vec3 front = course_point(course, at.front);
            const glm::vec3 rear = course_point(course, at.rear);
            glm::vec3 way = front - rear;
            constexpr float kOnePoint = 1e-3f;
            way = glm::length(way) > kOnePoint ? glm::normalize(way) : course_direction(course, (at.front + at.rear) / 2);
            glm::vec3 up = course_up(course, at.front) + course_up(course, at.rear);
            up -= glm::dot(up, way) * way;
            if (glm::length(up) < 1e-6f) // Up along the way: any square to it will do
                up = std::abs(way.y) < 0.9f ? glm::vec3 { 0, 1, 0 } - way.y * way : glm::vec3 { 1, 0, 0 } - way.x * way;
            up = glm::normalize(up);
            glm::mat4 frame(1.0f);
            frame[0] = glm::vec4(glm::cross(up, way), 0);
            frame[1] = glm::vec4(up, 0);
            frame[2] = glm::vec4(way, 0);
            frame[3] = glm::vec4((front + rear) / 2.0f, 1);
            return frame;
        }
    } // namespace

    glm::mat4 moved_along(const TrackCourse& course, const glm::mat4& saved, Axles saved_at, float by)
    {
        const glm::mat4 from = frame_at(course, saved_at);
        const glm::mat4 to = frame_at(course, { saved_at.front + by, saved_at.rear + by });
        // The frame is a turn and a shift, so its inverse is the turn's transpose and the shift taken back.
        glm::mat4 back = glm::transpose(glm::mat4(glm::mat3(from)));
        back[3] = glm::vec4(-(glm::mat3(back) * glm::vec3(from[3])), 1);
        return to * back * saved;
    }

} // namespace rct3::park
