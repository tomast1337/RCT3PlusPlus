// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_course.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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
        const auto& segments = ride.track->segments;
        // A track in one stretch runs along all its pieces; one in several, along each stretch on its own.
        std::optional<TrackCourse> first;
        std::optional<TrackCourse> stopping;
        std::vector<glm::vec3> others;
        for (std::size_t s = 0; s < std::max<std::size_t>(segments.size(), 1); ++s)
        {
            const bool whole = segments.size() <= 1;
            const std::size_t from = whole ? 0 : segments[s].first_piece;
            const std::size_t to = whole ? ride.track->pieces.size() : std::min(from + segments[s].piece_count, ride.track->pieces.size());
            if (from >= to)
                continue;
            TrackCourse course;
            course.first_piece = from;
            bool whole_stretch = true;
            for (const save::TrackPiece& piece : std::span(ride.track->pieces).subspan(from, to - from))
            {
                const auto found = by_id.find(piece.scenery);
                if (found == by_id.end())
                {
                    whole_stretch = false;
                    break;
                }
                const save::PlacedObject& object = *found->second;
                const auto shape = shape_of(piece, object);
                if (!shape || shape->line.path.size() < 2)
                {
                    whole_stretch = false;
                    break;
                }
                const auto model = models.find(model_key(object));
                const glm::mat4 to_world = placed_transform(object, model != models.end() ? &model->second : nullptr, grid, fitted);
                CourseKind kind = shape->use == PieceUse::Station ? CourseKind::Station : shape->chain ? CourseKind::Chain : CourseKind::Plain;
                if ((shape->record.flags & kPieceCableLift) != 0)
                    kind = CourseKind::Cable;
                else if (kind == CourseKind::Plain && shape->record.role == kRoleBlockBrake)
                    kind = CourseKind::BlockBrake;
                course.reverse_lift = course.reverse_lift || (shape->record.flags & kPieceReverseLift) != 0;
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
            // Missing a piece, the track has no course; past the stretch trains run on, that stretch is left out.
            if (!whole_stretch && !stopping)
                return std::nullopt;
            if (!whole_stretch)
                continue;
            course.closed = whole ? ride.track->circuit : segments[s].closed;
            // shortcut: trains run only on the first stretch with a station, the others stand empty; upgrade when
            // a ride's trains are kept by stretch.
            if (std::ranges::contains(course.kinds, CourseKind::Station) && !course.points.empty())
            {
                if (stopping)
                    others.push_back(course.points.front());
                else
                    stopping = std::move(course);
            }
            else if (!first)
                first = std::move(course);
        }
        if (!stopping)
            return first;
        stopping->other_stations = std::move(others);
        return stopping;
    }

    std::vector<float> station_runs(const TrackCourse& course)
    {
        std::vector<float> runs;
        bool in_run = false;
        for (std::size_t k = 0; k + 1 < course.points.size(); ++k)
        {
            const float stretch = course.distance[k + 1] - course.distance[k];
            if (course.kinds[k] != CourseKind::Station)
                in_run = false;
            else if (in_run)
                runs.back() += stretch;
            else
            {
                runs.push_back(stretch);
                in_run = true;
            }
        }
        // A run that reaches the end of a closed course and one from its start are the same run.
        const bool starts_in_run = !course.kinds.empty() && course.kinds.front() == CourseKind::Station;
        if (course.closed && in_run && starts_in_run && runs.size() > 1)
        {
            runs.front() += runs.back();
            runs.pop_back();
        }
        return runs;
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

    int trains_fitting(const TrackCourse& course, std::span<const CarFit> fits)
    {
        float train = kTrainGap;
        for (const CarFit& fit : fits)
            train += fit.length;
        return std::max(static_cast<int>(course.length() / 2 / train), 1);
    }

    void stand_train(const TrackCourse& course, save::Train& train, std::span<const CarFit> fits, float front)
    {
        if (fits.empty())
            return;
        train.distance = front;
        train.length = 0;
        float at = front;
        for (std::size_t k = 0; k < train.cars.size(); ++k)
        {
            save::Car& car = train.cars[k];
            const CarFit& fit = fits[std::min(k, fits.size() - 1)];
            car.distance = at;
            car.length = fit.length;
            car.front_wheel_distance = at - fit.front_wheels;
            car.rear_wheel_distance = at - fit.rear_wheels;
            const glm::mat4 place = glm::translate(frame_at(course, { car.front_wheel_distance, car.rear_wheel_distance }), -fit.on_track);
            car.position = { place[3].x, place[3].y, place[3].z };
            const glm::quat turn = glm::quat_cast(glm::mat3(place));
            const glm::vec3 axis = glm::angle(turn) > 0 ? glm::axis(turn) * glm::angle(turn) : glm::vec3(0.0f);
            car.orientation = { axis.x, axis.y, axis.z };
            car.position_valid = true;
            at -= fit.length;
            train.length += fit.length;
        }
    }

    std::optional<ArmAngles> arm_angles_at(const TrackCourse& course, std::span<const std::optional<ArmAngles>> held, float distance)
    {
        const std::size_t n = std::min(course.piece_starts.size(), held.size());
        const float length = course.length();
        if (n == 0 || length <= 0)
            return std::nullopt;
        if (course.closed)
            distance = std::fmod(std::fmod(distance, length) + length, length);
        const auto middle = [&](std::size_t i) { return (course.piece_starts[i] + (i + 1 < course.piece_starts.size() ? course.piece_starts[i + 1] : length)) / 2; };
        // The piece the car is on, and the one whose middle is on the other side of it.
        const std::size_t on = static_cast<std::size_t>(std::ranges::upper_bound(course.piece_starts.begin(), course.piece_starts.begin() + static_cast<std::ptrdiff_t>(n), distance) - course.piece_starts.begin()) - 1;
        const std::size_t piece = std::min(on, n - 1);
        const bool behind = distance < middle(piece);
        std::optional<std::size_t> other;
        if (behind && piece > 0)
            other = piece - 1;
        else if (!behind && piece + 1 < n)
            other = piece + 1;
        else if (course.closed)
            other = behind ? n - 1 : 0;
        const std::optional<ArmAngles>& here = held[piece];
        const std::optional<ArmAngles> there = other ? held[*other] : std::nullopt;
        if (!here || !there)
            return here ? here : there;
        // From the earlier middle to the later one, a whole course further on where they wrap round.
        float from = behind ? middle(*other) : middle(piece);
        float to = behind ? middle(piece) : middle(*other);
        if (to < from)
            (behind ? from : to) += behind ? -length : length;
        float at = distance;
        if (at < from)
            at += length;
        const float t = to > from ? std::clamp((at - from) / (to - from), 0.0f, 1.0f) : 0.0f;
        const ArmAngles& a = behind ? *there : *here;
        const ArmAngles& b = behind ? *here : *there;
        ArmAngles out;
        for (std::size_t j = 0; j < out.size(); ++j)
            out[j] = a[j] + (b[j] - a[j]) * t;
        return out;
    }

} // namespace rct3::park
