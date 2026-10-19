// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "park/ground_paths.hpp"
#include "park/queue_fences.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park_state.hpp"
#include "simulation/ratings.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        bool contains(std::string_view text, std::string_view word)
        {
            return std::ranges::search(text, word, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }).begin() != text.end();
        }

        /// Drink, food or umbrella, guessed from a name.
        // shortcut: matches words in item names and kind symbols (Soda:cid), upgrade to the kinds' own food and
        // drink values once those are read.
        std::optional<Product> product_named(std::string_view name)
        {
            for (const std::string_view w : { "drink", "cola", "soda", "juice", "coffee", "lemonade", "shake", "smoothie", "moonshine", "water", "beer", "burdock", "hotchocolate", "hot chocolate" })
                if (contains(name, w))
                    return Product::Drink;
            for (const std::string_view w : { "burger", "fries", "fry", "chips", "pizza", "hotdog", "hot dog", "popcorn", "candy", "cotton", "ice cream", "icecream", "donut", "chicken", "pretzel", "food", "noodle", "taco", "sandwich", "cookie", "nugget", "kebab", "sub", "steak", "pie", "sweets", "floss", "toffee", "delight", "hershey" })
                if (contains(name, w))
                    return Product::Food;
            if (contains(name, "umbrella"))
                return Product::Umbrella;
            return std::nullopt;
        }

        /// Whether food or drink by that name is served cold or hot, guessed from the name as product_named does.
        Serve serve_named(std::string_view name)
        {
            for (const std::string_view w : { "ice cream", "icecream", "shake", "slush", "frozen", "smoothie", "lolly", "sundae", "sorbet" })
                if (contains(name, w))
                    return Serve::Cold;
            for (const std::string_view w : { "coffee", "hotchocolate", "hot chocolate", "cocoa", "soup" })
                if (contains(name, w))
                    return Serve::Hot;
            return Serve::Any;
        }

        /// Whether a ride by that kind's symbol splashes its riders: boats, flumes, rapids and water slides.
        // shortcut: matches words in the ride kinds' symbols, upgrade when rides' own water flags are read.
        bool splashes(std::string_view symbol)
        {
            return std::ranges::any_of(std::initializer_list<std::string_view> { "flume", "rapids", "splash", "dingy", "dinghy", "raft", "bumperboat", "jetski", "watercoaster", "supersoaker", "seaserpent", "bodyslide", "ringslide", "proslide", "sinkingship", "airboat" }, [&](std::string_view w) { return contains(symbol, w); });
        }

        /// The first item of a stall that is a drink, food or umbrella, by its name or else its kind's symbol (custom
        /// stalls keep only a placeholder name); where neither tells, by the stall's own symbol, at its first item's price.
        struct Sale
        {
            Shop shop;
            std::int64_t price = 0;
        };
        std::optional<Sale> sale_of(const save::Stall& stall)
        {
            for (const save::StallItem& item : stall.items)
            {
                if (const auto product = product_named(item.name))
                    return Sale { { *product, serve_named(item.name) }, item.price };
                if (const auto product = product_named(item.kind_symbol))
                    return Sale { { *product, serve_named(item.kind_symbol) }, item.price };
            }
            if (const auto product = product_named(stall.base.symbol); product && !stall.items.empty())
                return Sale { { *product, serve_named(stall.base.symbol) }, stall.items.front().price };
            return std::nullopt;
        }
        /// The footpath node on the tile `o` stands on, or on one beside it.
        std::optional<NodeId> path_by(const PathNetwork& paths, const save::PlacedObject& o)
        {
            constexpr std::array<glm::vec2, 5> kAround { { { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } };
            for (const glm::vec2 d : kAround)
            {
                const glm::vec3 middle { paths.origin.x + (static_cast<float>(o.col) + 0.5f + d.x) * paths.tile_size, static_cast<float>(o.base_height.value_or(0)) * paths.level_height, paths.origin.y + (static_cast<float>(o.row) + 0.5f + d.y) * paths.tile_size };
                if (const auto at = paths.nearest(middle); at && !paths.nodes()[*at].queue)
                    return at;
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
                    if (n.removed || n.queue != queues || n.tile.col != spot.col + dc || n.tile.row != spot.row + dr)
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

        /// Where `path` on `grid` sits in the network: its tile and level, and the height of its middle.
        std::pair<PathTileSpot, float> node_spot(const PathNetwork& paths, const save::PathTile& p, const save::TerrainGrid& grid)
        {
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
            return { { p.col, p.row, static_cast<int>(std::lround(height / paths.level_height)) }, height };
        }

        /// How a ride's station meets the paths.
        struct RideLinks
        {
            std::vector<NodeId> line; ///< Its queue line from the entrance back, or empty
            NodeId entrance = kNoNode;
            NodeId exit = kNoNode;
        };

        /// Guests line up on the queue that reaches the entrance, or walk straight in from a path; none when
        /// neither reaches it.
        std::optional<RideLinks> ride_links(const PathNetwork& paths, const save::Station& station)
        {
            if (!station.entrance_spot)
                return std::nullopt;
            const save::PlacementSpot& at = *station.entrance_spot;
            RideLinks links;
            if (const NodeId front = beside(paths, at, at.direction, true); front != kNoNode && station.queue_line != 0 && paths.nodes()[front].line == station.queue_line)
                links.line = queue_line(paths, front);
            links.entrance = links.line.empty() ? beside(paths, at, at.direction, false) : links.line.back();
            if (links.entrance == kNoNode)
                return std::nullopt;
            if (station.exit_spot)
                links.exit = beside(paths, *station.exit_spot, station.exit_spot->direction, false);
            return links;
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

    std::size_t import_cover(SimWorld& world, const std::function<std::vector<park::Overhead>(std::span<const glm::vec3>)>& overhead)
    {
        PathNetwork& paths = world.paths;
        std::vector<glm::vec3> middles;
        for (NodeId n = 0; n < paths.nodes().size(); ++n)
            middles.push_back(paths.centre(n));
        const std::vector<park::Overhead> over = overhead(middles);
        std::size_t roofed = 0;
        for (NodeId n = 0; n < paths.nodes().size() && n < over.size(); ++n)
        {
            paths.cover(n, over[n].roof, over[n].shade);
            roofed += over[n].roof ? 1 : 0;
        }
        return roofed;
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
            (void)add_path(world, p, grid);
    }

    NodeId add_path(SimWorld& world, const save::PathTile& path, const save::TerrainGrid& grid)
    {
        if (path.kind == save::PathKind::Unknown)
            return kNoNode;
        const auto [spot, height] = node_spot(world.paths, path, grid);
        const std::uint64_t line = path.queue ? path.queue->queue_line_ref : 0;
        const NodeId id = world.paths.add(spot, path.kind == save::PathKind::Queue, height, line);
        world.paths.set_line(id, line);
        world.paths.set_sloped(id, path.elevation && park::path_rise(path.elevation->slope) > 0);
        return id;
    }

    void remove_path(SimWorld& world, const save::PathTile& path, const save::TerrainGrid& grid)
    {
        PathNetwork& paths = world.paths;
        const auto gone = paths.find(node_spot(paths, path, grid).first);
        if (!gone)
            return;
        paths.remove(*gone);
        auto& r = world.registry;
        for (const auto [e, stride, pose] : r.view<Stride, const Pose>().each())
        {
            const bool crossing = std::ranges::contains(stride.route, *gone);
            if (stride.from != *gone && stride.to != *gone && !crossing)
                continue;
            if (stride.from == *gone)
            {
                // Onto another path on the same tile, if there is one; otherwise left standing where it was.
                // shortcut: someone on a tile left without paths stays there; move them to the nearest path
                // by distance if that turns out to matter.
                const auto other = paths.nearest(pose.position);
                stride.from = other.value_or(*gone);
            }
            stride.to = stride.from;
            stride.progress = 1;
            stride.route.clear();
            if (auto* errand = r.try_get<Errand>(e); errand != nullptr && errand->kind == ErrandKind::Heading)
                *errand = { };
        }
        std::vector<entt::entity> swept;
        for (const auto [e, litter] : r.view<const Litter>().each())
            if (litter.node == *gone)
                swept.push_back(e);
        r.destroy(swept.begin(), swept.end());
    }

    std::size_t relink_attractions(SimWorld& world, const save::RideModel& rides)
    {
        auto& r = world.registry;
        std::size_t changed = 0;
        for (const auto [e, attraction] : r.view<Attraction>().each())
        {
            if (attraction.saved == 0)
                continue;
            if (auto* cycle = r.try_get<CycleRide>(e))
            {
                const auto ride = std::ranges::find(rides.rides, attraction.saved, [](const save::Ride& x) { return x.base.id; });
                if (ride == rides.rides.end() || ride->stations.empty() || !cycle->queue.empty())
                    continue;
                const auto links = ride_links(world.paths, ride->stations.front());
                if (!links || (links->entrance == attraction.entrance && links->line == cycle->line && links->exit == cycle->exit))
                    continue;
                attraction.entrance = links->entrance;
                cycle->line = links->line;
                cycle->exit = links->exit;
                ++changed;
                continue;
            }
            const auto stall = std::ranges::find(rides.stalls, attraction.saved, [](const save::Stall& x) { return x.base.id; });
            if (stall == rides.stalls.end() || !stall->base.spot)
                continue;
            const NodeId entrance = beside(world.paths, *stall->base.spot, stall->base.facing, false);
            if (entrance == kNoNode || entrance == attraction.entrance)
                continue;
            attraction.entrance = entrance;
            ++changed;
        }
        return changed;
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
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = stall.base.name, .entrance = entrance, .price = sale->price, .open = stall.base.state == save::AttractionState::Open, .saved = stall.base.id });
            r.emplace<Shop>(e, sale->shop);
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

    Trains trains_of(const Coaster& coaster, const save::Ride& ride)
    {
        Trains out;
        for (const save::Train& saved : ride.trains)
        {
            // Where the park kept it; a train away from the start runs on round to it, empty.
            Coaster train = coaster;
            const float length = train.length();
            train.at = length > 0 ? std::fmod(std::fmod(saved.distance, length) + length, length) : 0;
            train.travelled = length > 0 ? std::fmod(train.at - train.start + length, length) : 0;
            constexpr float kAtStart = 0.5f; // Metres off the start that still count as standing at it
            if (train.travelled < kAtStart || train.travelled > length - kAtStart)
            {
                train.at = train.start;
                train.travelled = 0;
            }
            train.running = train.travelled > 0;
            train.speed = train.running ? saved.speed : 0;
            out.trains.push_back(train);
        }
        if (out.trains.empty())
            out.trains.push_back(coaster);
        out.riders.resize(out.trains.size());
        return out;
    }

    std::optional<Product> facility_of(std::uint32_t kind)
    {
        // Checked against the game's special attractions: kind 11 is named ToiletName, 12 CashMachineName and 13
        // MedicalCentreName in their items.
        switch (kind)
        {
            case 11:
                return Product::Toilet;
            case 12:
                return Product::CashMachine;
            case 13:
                return Product::FirstAid;
            default:
                return std::nullopt;
        }
    }

    std::size_t import_facilities(SimWorld& world, const save::RideModel& rides, const std::function<std::optional<std::uint32_t>(const save::Ride&)>& kind_of)
    {
        std::size_t added = 0;
        for (const save::Ride& ride : rides.rides)
        {
            if (ride.kind != save::RideKind::Special || !ride.base.spot)
                continue;
            const auto kind = kind_of(ride);
            const auto product = kind ? facility_of(*kind) : std::nullopt;
            const NodeId entrance = product ? beside(world.paths, *ride.base.spot, ride.base.facing, false) : kNoNode;
            if (entrance == kNoNode)
                continue;
            auto& r = world.registry;
            const entt::entity e = r.create();
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = ride.base.name, .entrance = entrance, .price = std::max<std::int64_t>(ride.ticket_price, 0), .open = ride.base.state == save::AttractionState::Open, .saved = ride.base.id });
            r.emplace<Shop>(e, *product);
            ++added;
        }
        return added;
    }

    std::size_t import_sights(SimWorld& world, std::span<const save::PlacedObject> objects, const std::function<std::optional<PathItemKind>(const save::PlacedObject&)>& sight_of)
    {
        std::size_t added = 0;
        for (const save::PlacedObject& o : objects)
        {
            const auto kind = o.hidden ? std::nullopt : sight_of(o);
            if (!kind)
                continue;
            if (const auto at = path_by(world.paths, o))
            {
                world.registry.emplace<PathItem>(world.registry.create(), PathItem { *at, *kind });
                ++added;
            }
        }
        return added;
    }

    entt::entity import_gate(SimWorld& world, std::span<const save::PlacedObject> objects, const std::function<bool(const save::PlacedObject&)>& is_gate)
    {
        for (const save::PlacedObject& o : objects)
        {
            if (o.hidden || !is_gate(o))
                continue;
            if (const auto at = path_by(world.paths, o))
            {
                const entt::entity e = world.registry.create();
                world.registry.emplace<ParkGate>(e, *at);
                return e;
            }
        }
        return entt::null;
    }

    Upkeep upkeep_of(const save::Ride& ride)
    {
        // The intervals the maintenance page lists, in its order: every 10, 20, 30 and 45 minutes, every hour,
        // every two hours, never.
        constexpr std::array<float, 7> kIntervals { 600, 1200, 1800, 2700, 3600, 7200, -1 };
        const std::int32_t interval = ride.breakdown.inspection_interval;
        using enum save::RepairStatus;
        const save::RepairStatus status = ride.base.repair_status;
        // Broken by the repair status alone: saves keep a breakdown kind on most open, running rides too.
        const bool broken = status == MechanicCalled || status == MechanicOnTheWay || status == BeingFixed;
        return {
            .reliability = std::clamp(ride.breakdown.reliability / 100, 0.0f, 1.0f),
            .broken = broken,
            .since_inspection = std::max(ride.base.time_since_inspection, 0.0f),
            .inspect_every = interval >= 0 && std::cmp_less(interval, kIntervals.size()) ? kIntervals[static_cast<std::size_t>(interval)] : Upkeep { }.inspect_every,
            .kind = broken ? static_cast<Breakdown>(std::to_underlying(ride.breakdown.current)) : Breakdown::None,
            .last = static_cast<Breakdown>(std::to_underlying(ride.breakdown.last)),
            .downtime = ride.breakdown.downtime_windows,
        };
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
                // Each train carries its share of the ride's seats.
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size() / std::max<std::size_t>(coaster ? ride.trains.size() : 1, 1)), .seconds = ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0 };
            if (!timing || ride.stations.empty() || !ride.stations.front().entrance_spot)
                continue;
            auto links = ride_links(world.paths, ride.stations.front());
            if (!links)
                continue;
            const NodeId entrance = links->entrance;
            const NodeId exit = links->exit;
            std::vector<NodeId> line = std::move(links->line);
            auto& r = world.registry;
            const entt::entity e = r.create();
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = entrance, .price = ride.ticket_price, .open = ride.base.state == save::AttractionState::Open, .saved = ride.base.id });
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
            if (ride.assessment.valid)
                cycle.cover = std::clamp(ride.assessment.under_cover, 0.0f, 1.0f);
            cycle.splash = splashes(ride.base.symbol);
            r.emplace<CycleRide>(e, std::move(cycle));
            if (coaster)
                r.emplace<Trains>(e, trains_of(*coaster, ride));
            r.emplace<Upkeep>(e, upkeep_of(ride));
            if (r.get<Upkeep>(e).broken)
                r.get<Attraction>(e).open = false;
            // The game offers the settings to tracked rides only; others hold leftovers.
            if (ride.kind == save::RideKind::Tracked)
                r.emplace<Departure>(e, departure_of(ride.options));
            added[i] = e;
        }
        return added;
    }

    std::size_t import_litter(SimWorld& world, const save::PeopleModel& people, float now)
    {
        std::size_t added = 0;
        for (const save::Litter& l : people.litter)
        {
            const auto at = l.state == save::LitterState::OnGround ? world.paths.nearest({ l.position.x, l.position.y, l.position.z }) : std::nullopt;
            if (!at)
                continue;
            const float age = std::max(now - l.create_time, 0.0f) * kSecondsPerHour / kCalendarUnitsPerHour;
            world.registry.emplace<Litter>(world.registry.create(), Litter { *at, l.type, age });
            ++added;
        }
        return added;
    }

    std::size_t import_path_items(SimWorld& world, std::span<const save::PlacedObject> placed, std::span<const save::BinState> bins, std::span<const std::uint64_t> benches, std::span<const save::SignState> signs)
    {
        // Approximation: Fun Valley's saved bins hold 0.4 and fill in steps of a hundredth, so a piece of rubbish
        // is taken to be one.
        constexpr float kSavedPiece = 0.01f;
        std::unordered_map<std::uint64_t, const save::PlacedObject*> objects;
        for (const save::PlacedObject& o : placed)
            objects.emplace(o.id, &o);
        const PathNetwork& paths = world.paths;
        auto& r = world.registry;
        std::size_t added = 0;
        const auto add = [&](std::uint64_t object, PathItemKind kind) -> entt::entity {
            const auto found = objects.find(object);
            if (found == objects.end())
                return entt::null;
            const save::PlacedObject& o = *found->second;
            const glm::vec3 middle { paths.origin.x + (static_cast<float>(o.col) + 0.5f) * paths.tile_size, static_cast<float>(o.base_height.value_or(0)) * paths.level_height, paths.origin.y + (static_cast<float>(o.row) + 0.5f) * paths.tile_size };
            const auto at = paths.nearest(middle);
            if (!at)
                return entt::null;
            const entt::entity e = r.create();
            r.emplace<PathItem>(e, PathItem { *at, kind });
            ++added;
            return e;
        };
        for (const save::BinState& b : bins)
            if (const entt::entity e = add(b.object_ref, PathItemKind::Bin); e != entt::null)
                r.emplace<Bin>(e, Bin { b.capacity / kSavedPiece, std::round(b.contents / kSavedPiece) });
        for (const std::uint64_t bench : benches)
            add(bench, PathItemKind::Bench);
        for (const save::SignState& s : signs)
            add(s.object_ref, PathItemKind::Sign);
        return added;
    }

    std::vector<entt::entity> import_staff(SimWorld& world, const save::PeopleModel& people)
    {
        auto& r = world.registry;
        std::vector<entt::entity> out(people.staff.size(), entt::null);
        for (std::size_t i = 0; i < people.staff.size(); ++i)
        {
            const save::Person& p = people.staff[i].person;
            const glm::vec3 position { p.position.x, p.position.y, p.position.z };
            const auto at = world.paths.nearest(position);
            if (!at || world.paths.nodes()[*at].queue)
                continue;
            using enum save::StaffKind;
            const save::StaffKind kind = people.staff[i].kind;
            const StaffRole role = kind == Handyman ? StaffRole::Handyman : kind == Mechanic ? StaffRole::Mechanic : kind == SecurityGuard ? StaffRole::Guard : StaffRole::Other;
            const entt::entity e = r.create();
            r.emplace<Staff>(e, Staff { .role = role, .wage = kWagePerLevel * std::max(people.staff[i].wage_level, 1) });
            r.emplace<Pose>(e, position, -p.heading, position, -p.heading); // The original turns the other way
            start_walking(world, e, *at);
            out[i] = e;
        }
        return out;
    }

    Departure departure_of(const save::OperatingOptions& options)
    {
        const bool listed = options.wait_for_load >= 0 && options.wait_for_load <= static_cast<std::int32_t>(LoadWait::Any);
        return { .wait_for = listed ? static_cast<LoadWait>(options.wait_for_load) : LoadWait::Any, .min_wait = std::max(options.min_wait, -1), .max_wait = std::max(options.max_wait, -1) };
    }

    std::size_t board_saved_riders(SimWorld& world, const save::PeopleModel& people, std::span<const entt::entity> guests, const save::RideModel& saved, std::span<const entt::entity> rides)
    {
        auto& r = world.registry;
        std::unordered_map<save::ObjectId, entt::entity> ride_of;
        for (std::size_t i = 0; i < saved.rides.size() && i < rides.size(); ++i)
            if (rides[i] != entt::null)
                ride_of.emplace(saved.rides[i].base.id, rides[i]);
        std::size_t seated = 0;
        for (std::size_t i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const save::Person& p = people.guests[i].person;
            const auto ride = ride_of.find(p.state_target.id);
            const entt::entity e = guests[i];
            if (e == entt::null || p.state != save::PersonState::OnRide || ride == ride_of.end())
                continue;
            auto& cycle = r.get<CycleRide>(ride->second);
            // Off the ride they get off where its riders do.
            if (!r.all_of<Stride>(e))
                start_walking(world, e, cycle.exit != kNoNode ? cycle.exit : r.get<Attraction>(ride->second).entrance);
            std::vector<entt::entity>* riders = nullptr;
            if (auto* fleet = r.try_get<Trains>(ride->second))
            {
                // A running train with room first, as most riders the park kept are out on the track.
                for (int pass = 0; pass < 2 && riders == nullptr; ++pass)
                    for (std::size_t t = 0; t < fleet->trains.size() && riders == nullptr; ++t)
                        if (fleet->riders[t].size() < cycle.capacity && (pass == 1 || fleet->trains[t].running))
                        {
                            riders = &fleet->riders[t];
                            fleet->trains[t].running = true;
                        }
            }
            else if (cycle.riders.size() < cycle.capacity)
            {
                riders = &cycle.riders;
                if (cycle.timer == 0)
                    cycle.timer = std::max<std::uint32_t>(cycle.cycle_ticks, 1);
            }
            if (riders == nullptr)
                continue;
            riders->push_back(e);
            r.get<Errand>(e) = { ErrandKind::Riding, ride->second, 0 };
            ++seated;
        }
        return seated;
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
