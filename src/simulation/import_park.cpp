// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "park/ground_paths.hpp"
#include "park/queue_fences.hpp"
#include "simulation/components.hpp"
#include "simulation/ratings.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace rct3::simulation
{

    namespace
    {
        bool contains(std::string_view text, std::string_view word)
        {
            return std::ranges::search(text, word, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }).begin() != text.end();
        }

        /// The first item of a stall that is a drink or food, guessed from its name.
        // shortcut: matches item names against a few words, upgrade to the item's asset kind once that is decoded.
        std::optional<std::pair<Product, std::int64_t>> sale_of(const save::Stall& stall)
        {
            for (const save::StallItem& item : stall.items)
            {
                for (const std::string_view w : { "drink", "cola", "soda", "juice", "coffee", "lemonade", "milkshake" })
                    if (contains(item.name, w))
                        return std::pair { Product::Drink, item.price };
                for (const std::string_view w : { "burger", "fries", "chips", "pizza", "hotdog", "hot dog", "popcorn", "candy", "cotton", "ice cream", "icecream", "donut", "chicken", "pretzel", "food", "noodle", "taco", "sandwich" })
                    if (contains(item.name, w))
                        return std::pair { Product::Food, item.price };
            }
            return std::nullopt;
        }
        /// The path node beside `spot` nearest its height, trying the side `facing` first; queue tiles only with
        /// `queues`, otherwise footpaths only. kNoNode when none touches it.
        NodeId beside(const PathNetwork& paths, const save::PlacementSpot& spot, std::uint8_t facing, bool queues)
        {
            NodeId found = kNoNode;
            int gap = 0;
            constexpr std::array<std::array<int, 2>, 4> kSides { { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } };
            for (int k = 0; k < 4; ++k)
            {
                const auto& [dc, dr] = kSides[static_cast<std::size_t>((facing + k) % 4)];
                for (const PathNode& n : paths.nodes())
                {
                    if (n.queue != queues || n.tile.col != spot.col + dc || n.tile.row != spot.row + dr)
                        continue;
                    const int g = std::abs(n.tile.level - spot.height);
                    if (found == kNoNode || g < gap)
                    {
                        found = *paths.find(n.tile);
                        gap = g;
                    }
                }
            }
            return found;
        }

        /// The tiles of the queue line `front` belongs to, from `front` back along the line to its far end.
        // shortcut: follows the first way on at a fork, upgrade if lines that split are ever met.
        std::vector<NodeId> queue_line(const PathNetwork& paths, NodeId front)
        {
            const std::uint64_t id = paths.nodes()[front].line;
            std::vector<NodeId> line { front };
            for (NodeId at = front;;)
            {
                NodeId next = kNoNode;
                for (const NodeId n : paths.nodes()[at].next)
                    if (n != kNoNode && paths.nodes()[n].queue && paths.nodes()[n].line == id && std::ranges::find(line, n) == line.end())
                    {
                        next = n;
                        break;
                    }
                if (next == kNoNode)
                    return line;
                line.push_back(next);
                at = next;
            }
        }

        /// A rating as saved, about 0 to 10, as a fraction.
        float rating(float saved)
        {
            return std::clamp(saved / 10.0f, 0.0f, 1.0f);
        }
    } // namespace

    std::optional<RideTiming> flat_ride_timing(std::span<const float> animation_seconds, std::uint32_t seats, std::int32_t loops)
    {
        // Flat rides list their animations as standing, starting, running and stopping.
        if (animation_seconds.size() < 4 || seats == 0)
            return std::nullopt;
        const float seconds = animation_seconds[1] + static_cast<float>(std::max(loops, 1)) * animation_seconds[2] + animation_seconds[3];
        if (seconds <= 0)
            return std::nullopt;
        return RideTiming { .seats = seats, .seconds = seconds };
    }

    std::vector<std::optional<RideTiming>> flat_ride_timings(const save::RideModel& rides, std::span<const save::PlacedObject> objects, const std::function<const park::ObjectModel*(const save::PlacedObject&)>& model_of)
    {
        std::vector<std::optional<RideTiming>> out(rides.rides.size());
        for (std::size_t i = 0; i < rides.rides.size(); ++i)
        {
            const save::Ride& ride = rides.rides[i];
            if (ride.kind != save::RideKind::Flat || ride.base.placement == 0)
                continue;
            // The ride stands as the objects hanging from its placement; the one with seats is the ride itself.
            for (const save::PlacedObject& o : objects)
            {
                if (o.owner_ref != ride.base.placement)
                    continue;
                if (const park::ObjectModel* model = model_of(o))
                    if ((out[i] = flat_ride_timing(model->animation_seconds, model->seats, ride.loop_count)))
                        break;
            }
        }
        return out;
    }

    void import_paths(SimWorld& world, const save::WorldModel& world_model)
    {
        const save::TerrainGrid& grid = world_model.terrain;
        PathNetwork& paths = world.paths;
        paths.tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : paths.tile_size;
        paths.origin = { grid.origin_x, grid.origin_z };
        paths.level_height = 1;
        // A steep slope climbs two units across a tile, and so may the ground under paths laid on it.
        // shortcut: joins tiles by the levels at their middles, upgrade to matching the heights of shared edges
        // if stacked paths two units apart ever join wrongly.
        paths.max_step = 2;
        for (const save::PathTile& p : world_model.paths)
        {
            if (p.kind == save::PathKind::Unknown)
                continue;
            float height = 0;
            if (p.elevation && p.elevation->slope != save::kSlopeOnGround)
            {
                // The stored height is at one end of a sloped piece; its middle is half the rise up or down.
                const float rise = park::path_rise(p.elevation->slope);
                height = static_cast<float>(p.elevation->quantised_height) + (park::slope_falls(p.elevation->slope) ? -rise : rise) / 2;
            }
            else
            {
                const float x = paths.origin.x + (static_cast<float>(p.col) + 0.5f) * paths.tile_size;
                const float z = paths.origin.y + (static_cast<float>(p.row) + 0.5f) * paths.tile_size;
                height = grid.height_at(x, z).value_or(0.0f);
            }
            paths.add({ p.col, p.row, static_cast<int>(std::lround(height / paths.level_height)) }, p.kind == save::PathKind::Queue, height, p.queue ? p.queue->queue_line_ref : 0);
        }
    }

    std::size_t import_stalls(SimWorld& world, const save::RideModel& rides)
    {
        std::size_t added = 0;
        for (const save::Stall& stall : rides.stalls)
        {
            const auto sale = sale_of(stall);
            if (!sale || !stall.base.spot)
                continue;
            const NodeId entrance = beside(world.paths, *stall.base.spot, stall.base.facing, false);
            if (entrance == kNoNode)
                continue;
            auto& r = world.registry;
            const entt::entity e = r.create();
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = stall.base.name, .entrance = entrance, .price = sale->second, .open = stall.base.state == save::AttractionState::Open });
            r.emplace<Shop>(e, sale->first);
            ++added;
        }
        return added;
    }

    std::optional<Coaster> coaster_of(const park::TrackCourse& course, const save::Ride& ride)
    {
        if (!course.closed || std::ranges::find(course.kinds, park::CourseKind::Station) == course.kinds.end())
            return std::nullopt;
        Coaster c;
        c.distance = course.distance;
        for (const glm::vec3& p : course.points)
            c.height.push_back(p.y);
        for (const park::CourseKind k : course.kinds)
            c.kinds.push_back(k == park::CourseKind::Chain ? Stretch::Chain : k == park::CourseKind::Station ? Stretch::Station : Stretch::Plain);
        // Trains set off from where a station stretch ends.
        for (std::size_t i = 0; i + 1 < c.kinds.size(); ++i)
            if (c.kinds[i] == Stretch::Station && c.kinds[i + 1] != Stretch::Station)
            {
                c.start = c.distance[i + 1];
                break;
            }
        c.at = c.start;
        c.lift_speed = ride.options.lift_speed;
        // shortcut: stations push at the lift speed, upgrade when the station's own speed is read.
        c.station_speed = ride.options.lift_speed;
        if (ride.options.fixed_speed > 0)
            c.fixed_speed = ride.options.fixed_speed;
        if (!ride.trains.empty() && ride.trains[0].length > 0)
            c.train_length = ride.trains[0].length;
        return c;
    }

    std::vector<entt::entity> import_rides(SimWorld& world, const save::RideModel& rides, std::span<const std::optional<RideTiming>> flat_timings, std::span<const std::optional<Coaster>> coasters)
    {
        std::vector<entt::entity> added(rides.rides.size(), entt::null);
        for (std::size_t i = 0; i < rides.rides.size(); ++i)
        {
            const save::Ride& ride = rides.rides[i];
            std::optional<RideTiming> timing;
            const Coaster* coaster = i < coasters.size() && coasters[i] ? &*coasters[i] : nullptr;
            if (ride.kind == save::RideKind::Flat && i < flat_timings.size())
                timing = flat_timings[i];
            else if (ride.kind == save::RideKind::Tracked && !ride.seats.empty() && (coaster || (ride.stats.last_lap && ride.stats.last_lap->ride_time > 0)))
                // shortcut: one train carries all the ride's seats round, upgrade when each train runs on its own.
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size()), .seconds = ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0 };
            if (!timing || ride.stations.empty() || !ride.stations.front().entrance_spot)
                continue;
            const save::Station& station = ride.stations.front();
            const save::PlacementSpot& at = *station.entrance_spot;
            // Guests line up on the queue that reaches the entrance, or walk straight in from a path.
            std::vector<NodeId> line;
            if (const NodeId front = beside(world.paths, at, at.direction, true); front != kNoNode && station.queue_line != 0 && world.paths.nodes()[front].line == station.queue_line)
                line = queue_line(world.paths, front);
            const NodeId entrance = line.empty() ? beside(world.paths, at, at.direction, false) : line.back();
            if (entrance == kNoNode)
                continue;
            NodeId exit = kNoNode;
            if (station.exit_spot)
                exit = beside(world.paths, *station.exit_spot, station.exit_spot->direction, false);
            auto& r = world.registry;
            const entt::entity e = r.create();
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = entrance, .price = ride.ticket_price, .open = ride.base.state == save::AttractionState::Open });
            // Rated as the park has it; else from its last test, or a test run of our own.
            std::optional<Ratings> rated;
            if (ride.assessment.valid)
                rated = Ratings { ride.assessment.excitement, ride.assessment.intensity, ride.assessment.nausea };
            else if (ride.stats.last_lap)
                rated = rate(*ride.stats.last_lap);
            else if (const auto test = coaster ? test_lap(*coaster) : std::nullopt)
                rated = rate(*test);
            CycleRide cycle { .capacity = timing->seats, .cycle_ticks = ticks(timing->seconds), .line = std::move(line), .exit = exit };
            if (rated)
            {
                cycle.excitement = rating(rated->excitement);
                cycle.intensity = rating(rated->intensity);
                cycle.nausea = rating(rated->nausea);
            }
            r.emplace<CycleRide>(e, std::move(cycle));
            if (coaster)
                r.emplace<Coaster>(e, *coaster);
            added[i] = e;
        }
        return added;
    }

    std::size_t start_guests(SimWorld& world)
    {
        auto& r = world.registry;
        const auto view = r.view<const Pose, const Visitor>();
        std::vector<entt::entity> guests(view.begin(), view.end());
        std::ranges::sort(guests);
        // Each queue tile of a ride's line, by node: the ride and how far the tile is from the front.
        std::unordered_map<NodeId, std::pair<entt::entity, std::size_t>> lines;
        const auto ride_view = r.view<const CycleRide>();
        for (const entt::entity e : ride_view)
        {
            const auto& line = r.get<CycleRide>(e).line;
            for (std::size_t k = 0; k < line.size(); ++k)
                lines.emplace(line[k], std::pair { e, k });
        }
        std::vector<std::pair<std::size_t, entt::entity>> waiting; // By tile from the front, then guest
        std::size_t started = 0;
        for (const entt::entity e : guests)
        {
            const auto at = world.paths.nearest(r.get<Pose>(e).position);
            if (!at)
                continue;
            if (!world.paths.nodes()[*at].queue)
                start_walking(world, e, *at);
            else if (const auto on = lines.find(*at); on != lines.end())
            {
                start_walking(world, e, *at);
                auto& stride = r.get<Stride>(e);
                stride.along = static_cast<float>(on->second.second);
                r.get<Errand>(e) = { ErrandKind::Queueing, on->second.first, 0 };
                waiting.emplace_back(on->second.second, e);
            }
            else
                continue;
            ++started;
        }
        // Those nearer the front were there first.
        std::ranges::stable_sort(waiting, { }, &std::pair<std::size_t, entt::entity>::first);
        for (const auto& [k, e] : waiting)
            r.get<CycleRide>(r.get<Errand>(e).target).queue.push_back(e);
        return started;
    }

} // namespace rct3::simulation
