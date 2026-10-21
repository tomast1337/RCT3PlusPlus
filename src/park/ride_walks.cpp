// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_walks.hpp"

#include "ovl/float_value.hpp"
#include "ovl/integer_value.hpp"
#include "ovl/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace rct3::park
{

    namespace
    {
        constexpr float kTileEdge = 2; ///< From a building's middle to its tile's edge

        glm::vec3 at(const glm::mat4& place, float x, float y, float z)
        {
            return glm::vec3(place * glm::vec4(x, y, z, 1.0f));
        }

        std::size_t nearest(std::span<const glm::vec3> points, const glm::vec3& to)
        {
            std::size_t best = 0;
            float closest = std::numeric_limits<float>::infinity();
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                if (const float d = glm::distance(points[i], to); d < closest)
                {
                    closest = d;
                    best = i;
                }
            }
            return best;
        }

        /// The points of the closed `loop` from `from` to `to`, both in, the shorter way round.
        void go_round(std::vector<glm::vec3>& walk, std::span<const glm::vec3> loop, std::size_t from, std::size_t to)
        {
            const std::size_t n = loop.size();
            const std::size_t ahead = (to + n - from) % n;
            const bool forward = ahead <= n - ahead;
            const std::size_t steps = forward ? ahead : n - ahead;
            for (std::size_t k = 0; k <= steps; ++k)
                walk.push_back(loop[(forward ? from + k : from + n - k) % n]);
        }

        /// The point of the line through `origin` along `along` nearest `p`.
        glm::vec3 beside(const glm::vec3& origin, const glm::vec3& along, const glm::vec3& p)
        {
            const glm::vec3 way = glm::normalize(along);
            return origin + way * glm::dot(p - origin, way);
        }
    } // namespace

    Walkways placed_walkways(const Walkways& ways, const glm::mat4& place)
    {
        Walkways out;
        for (const glm::vec3& p : ways.loop)
            out.loop.push_back(at(place, p.x, p.y, p.z));
        for (const auto& path : ways.paths)
        {
            auto& moved = out.paths.emplace_back();
            for (const glm::vec3& p : path)
                moved.push_back(at(place, p.x, p.y, p.z));
        }
        return out;
    }

    std::expected<Platform, std::string> load_platform(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        Platform platform;
        const auto set = [&](std::string_view name, float value) {
            if (name == "PlatformHeight")
                platform.height = value;
            else if (name == "PlatformOffset")
                platform.offset = value;
            else if (name == "WalkAlongPlatform")
                platform.walk_along = value != 0;
            else if (name == "StepXOffset")
                platform.step_depth = value;
            else if (name == "StepXBase")
                platform.step_start = value;
            else if (name == "InsideEntranceExitXOffset")
                platform.inside = value;
        };
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::FloatItem& f : ovl::read_floats(*overlay, *part))
                set(f.name, f.value);
            for (const ovl::IntegerItem& i : ovl::read_integers(*overlay, *part))
                set(i.name, static_cast<float>(i.value));
        }
        return platform;
    }

    void set_ticket_spot(RideWays& ways, const glm::mat4& entrance, const glm::mat4& spot)
    {
        const glm::mat4 world = entrance * spot;
        ways.ticket = glm::vec3(world[3]);
        ways.ticket_heading = std::atan2(world[2].x, world[2].z);
    }

    RideWays flat_ride_ways(const Walkways& walkways, std::span<const glm::vec3> seats, const glm::mat4& entrance, const glm::mat4& exit)
    {
        RideWays ways;
        const glm::vec3 in = at(entrance, kTileEdge, 0, 0);
        const glm::vec3 out = at(exit, kTileEdge, 0, 0);
        const glm::vec3 outside = at(exit, -kTileEdge, 0, 0);
        for (const glm::vec3& seat : seats)
        {
            auto& on = ways.on.emplace_back(std::vector { in });
            auto& off = ways.off.emplace_back();
            // The path whose far end is nearest the seat.
            const std::vector<glm::vec3>* path = nullptr;
            float closest = std::numeric_limits<float>::infinity();
            for (const auto& p : walkways.paths)
            {
                if (!p.empty() && glm::distance(p.back(), seat) < closest)
                {
                    closest = glm::distance(p.back(), seat);
                    path = &p;
                }
            }
            if (walkways.loop.empty() || path == nullptr)
            {
                on.push_back(seat);
                off = { seat, out, outside };
                continue;
            }
            const std::size_t junction = nearest(walkways.loop, path->front());
            go_round(on, walkways.loop, nearest(walkways.loop, in), junction);
            // The loop's node stands for the path's first, which lies on it.
            on.insert(on.end(), path->begin() + 1, path->end());
            off.assign(path->rbegin(), path->rend() - 1);
            go_round(off, walkways.loop, junction, nearest(walkways.loop, out));
            off.push_back(out);
            off.push_back(outside);
        }
        return ways;
    }

    RideWays track_ride_ways(const Platform& platform, std::span<const glm::vec3> seats, const glm::mat4& entrance, const glm::mat4& exit)
    {
        RideWays ways;
        const float edge = kTileEdge + platform.offset;
        // Through the building, up the step and to the platform's edge; the same out, the other way.
        const auto climb = [&](const glm::mat4& place) { return std::vector { at(place, platform.inside, 0, 0), at(place, platform.step_start - platform.step_depth, 0, 0), at(place, edge - platform.step_depth, platform.height, 0), at(place, edge, platform.height, 0) }; };
        const std::vector<glm::vec3> up = climb(entrance);
        std::vector<glm::vec3> down = climb(exit);
        std::ranges::reverse(down);
        down.push_back(at(exit, -kTileEdge, 0, 0));
        for (const glm::vec3& seat : seats)
        {
            auto& on = ways.on.emplace_back(up);
            auto& off = ways.off.emplace_back();
            if (!platform.walk_along)
            {
                on.pop_back(); // Boards from the top of the step
                off.assign(down.begin() + 1, down.end());
                continue;
            }
            // Along the platform's edge, level with the seat.
            on.push_back(beside(up.back(), glm::vec3(entrance[2]), seat));
            off.push_back(beside(down.front(), glm::vec3(exit[2]), seat));
            off.insert(off.end(), down.begin(), down.end());
        }
        return ways;
    }

} // namespace rct3::park
