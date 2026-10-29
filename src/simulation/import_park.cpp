// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "ovl/animated_ride.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/path_decks.hpp"
#include "park/queue_fences.hpp"
#include "park/terrain_mesh.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park_state.hpp"
#include "simulation/ratings.hpp"
#include "simulation/staff_care.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
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

        /// Whether staff are kept off node `n`: off the park's land, or under one of its entrances.
        bool kept_from_staff(const SimWorld& world, const save::TerrainGrid& grid, NodeId n)
        {
            const PathTileSpot& t = world.paths.nodes()[n].tile;
            // Without terrain the whole park is its own.
            if (!grid.empty() && (!grid.contains(t.col, t.row) || (grid.at(t.col, t.row).ownership() != save::Ownership::Owned && grid.at(t.col, t.row).ownership() != save::Ownership::OwnedRights)))
                return true;
            const auto* under = world.registry.ctx().find<EntrancePaths>();
            return under != nullptr && std::ranges::contains(under->nodes, n);
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
                    if (n != kNoNode && paths.nodes()[n].queue && paths.nodes()[n].line == id && !std::ranges::contains(line, n))
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

        /// The path node, not a queue, nearest `at` anywhere in the park.
        // shortcut: guests saved in a pool, on a ride we do not bring over or in its queue step out to the nearest
        // path, as nothing moves them there; upgrade when swimming and those rides are simulated.
        std::optional<NodeId> walkway_near(const PathNetwork& paths, glm::vec3 at)
        {
            std::optional<NodeId> best;
            float gap = std::numeric_limits<float>::max();
            for (NodeId n = 0; n < paths.nodes().size(); ++n)
            {
                if (paths.nodes()[n].removed || paths.nodes()[n].queue)
                    continue;
                if (const float g = glm::distance(paths.centre(n), at); g < gap)
                {
                    gap = g;
                    best = n;
                }
            }
            return best;
        }

        /// A rating as saved, about 0 to 10, as a fraction.
        float rating(float saved)
        {
            return std::clamp(saved / 10.0f, 0.0f, 1.0f);
        }
    } // namespace

    std::optional<RideTiming> flat_ride_timing(std::span<const float> animation_seconds, std::uint32_t seats, std::int32_t loops)
    {
        if (animation_seconds.empty() || seats == 0)
            return std::nullopt;
        // Flat rides list their animations as standing, starting, running and stopping.
        if (animation_seconds.size() >= 4)
        {
            const RideTiming timing { .seats = seats, .start = animation_seconds[1], .run = animation_seconds[2], .stop = animation_seconds[3], .loops = static_cast<std::uint32_t>(std::max(loops, 1)) };
            if (timing.start + static_cast<float>(timing.loops) * timing.run + timing.stop > 0)
                return timing;
        }
        // Rides walked through or watched, a maze or a show, have no such cycle: they run once for their longest.
        // shortcut: a walk-through lasts as long as its animation, not as long as its riders take to walk it;
        // time it by its walkways once riders are timed walking.
        const float longest = std::ranges::max(animation_seconds);
        if (longest <= 0)
            return std::nullopt;
        return RideTiming { .seats = seats, .run = longest, .loops = 1 };
    }

    void stage_show(CycleRide& ride, const save::RideShow& show)
    {
        float seconds = 0;
        for (const std::uint32_t t : show.order)
            if (t < show.tricks.size())
                seconds += show.tricks[t].seconds;
        if (seconds <= 0)
            return; // Nothing to put on: the ride keeps its own timing
        ride.start.value = ride.stop.value = 0;
        ride.run.value = seconds;
        ride.loops = 1;
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

    namespace
    {
        /// What `pick` reads from each ride's item in its overlay, by the ride's place in `rides.rides`.
        template<typename Pick> std::vector<std::optional<std::uint32_t>> from_ride_items(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find, Pick pick)
        {
            // Each overlay's rides by symbol (tag included), read once.
            std::unordered_map<std::string, std::map<std::string, std::uint32_t, strings::LessIgnoringCase>> loaded;
            std::vector<std::optional<std::uint32_t>> out(rides.rides.size());
            for (std::size_t i = 0; i < rides.rides.size(); ++i)
            {
                const save::Ride& ride = rides.rides[i];
                auto [at, added] = loaded.try_emplace(ride.base.overlay_path);
                const auto file = added ? find(std::format("{}.common.ovl", ride.base.overlay_path)) : std::nullopt;
                if (auto overlay = file ? ovl::Overlay::load(*file) : std::unexpected(ovl::Error::MissingFile))
                {
                    for (const auto part : { maybe(&overlay->common()), overlay->unique() })
                    {
                        if (!part)
                            continue;
                        for (const auto& item : ovl::read_tracked_rides(*overlay, *part))
                            if (const std::optional<std::uint32_t> v = item.ride ? pick(*item.ride) : std::nullopt)
                                at->second[std::format("{}:trr", item.name)] = *v;
                        for (const auto& item : ovl::read_animated_rides(*overlay, *part))
                            if (const std::optional<std::uint32_t> v = item.ride ? pick(*item.ride) : std::nullopt)
                                at->second[std::format("{}:anr", item.name)] = *v;
                    }
                }
                if (const auto found = at->second.find(ride.base.symbol); found != at->second.end())
                    out[i] = found->second;
            }
            return out;
        }
    } // namespace

    std::vector<std::optional<std::uint32_t>> ride_guests(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) { return ride.guests; });
    }

    std::vector<std::optional<std::uint32_t>> ride_kinds(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) { return std::optional(ride.info.type_id); });
    }

    std::vector<std::optional<std::uint32_t>> ride_seatings(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) -> std::optional<std::uint32_t> {
            if constexpr (requires { ride.seating; })
                return ride.seating;
            return std::nullopt;
        });
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

    void import_paths(SimWorld& world, std::span<const save::PathTile> tiles, const save::TerrainGrid& grid)
    {
        PathNetwork& paths = world.paths;
        paths.tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : paths.tile_size;
        paths.origin = { grid.origin_x, grid.origin_z };
        paths.level_height = 1;
        // A steep slope climbs two units across a tile, and so may the ground under paths laid on it.
        // shortcut: joins tiles by the levels at their middles, upgrade to matching the heights of shared edges
        // if stacked paths two units apart ever join wrongly.
        paths.max_step = 2;
        for (const save::PathTile& p : tiles)
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
        // Its surface: the ground's own corners under a path on the ground, a ramp from the height of the edge behind
        // it to that of the edge it faces, else flat.
        std::array<float, 4> corners { height, height, height, height };
        if ((park::is_ground_queue(path) || (!path.elevation && path.kind == save::PathKind::Ground)) && grid.contains(path.col, path.row))
            corners = grid.at(path.col, path.row).corners;
        else if (path.elevation && park::path_rise(path.elevation->slope) > 0)
        {
            const std::uint8_t ahead = park::stored_direction(path.direction);
            const auto behind = static_cast<std::uint8_t>((ahead + 2) % 4);
            if (const auto up = park::edge_height(path, grid, ahead), down = park::edge_height(path, grid, behind); up && down)
                for (const std::uint8_t side : { ahead, behind })
                    for (const std::size_t k : park::edge_corners(side))
                        corners[k] = side == ahead ? *up : *down;
        }
        // A queue joins only across the two ends its line passes through.
        std::uint8_t open = 0b1111;
        if (path.queue)
        {
            open = 0;
            // path_decks counts the sides from +row, the network from +col: the same sides, each pair swapped.
            for (std::uint8_t side = 0; side < 4; ++side)
                if (park::edge_height(path, grid, side ^ 1U))
                    open |= static_cast<std::uint8_t>(1U << side);
        }
        world.paths.set_corners(id, corners, open);
        world.paths.keep_staff_off(id, kept_from_staff(world, grid, id));
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
            stride.progress.value = 1;
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
                if (ride == rides.rides.end() || ride->stations.empty())
                    continue;
                for (Stop& stop : cycle->stops)
                {
                    const auto links = stop.saved < ride->stations.size() && stop.queue.empty() ? ride_links(world.paths, ride->stations[stop.saved]) : std::nullopt;
                    if (!links || (links->entrance == stop.entrance && links->line == stop.line && links->exit == stop.exit))
                        continue;
                    stop.entrance = links->entrance;
                    stop.line = links->line;
                    stop.exit = links->exit;
                    ++changed;
                }
                const auto links = cycle->queue.empty() ? ride_links(world.paths, ride->stations.front()) : std::nullopt;
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

    void import_pools(SimWorld& world, const park::PoolMap& pools)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        const auto middle = [&](std::int32_t col, std::int32_t row, float height) { return glm::vec3 { paths.origin.x + (static_cast<float>(col) + 0.5f) * paths.tile_size, height * paths.level_height, paths.origin.y + (static_cast<float>(row) + 0.5f) * paths.tile_size }; };
        // Each square's complex, found by spreading from square to square side by side.
        std::map<std::pair<std::int32_t, std::int32_t>, const park::PoolSquare*> squares;
        for (const park::PoolSquare& s : pools.squares)
            squares.emplace(std::pair { s.col, s.row }, &s);
        constexpr std::array<std::array<std::int32_t, 2>, 4> kSides { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } }; // By turn
        std::map<std::pair<std::int32_t, std::int32_t>, std::size_t> complex_of;
        std::vector<SwimPool> complexes;
        for (const park::PoolSquare& first : pools.squares)
        {
            if (!complex_of.try_emplace({ first.col, first.row }, complexes.size()).second)
                continue;
            SwimPool& complex = complexes.emplace_back();
            for (std::vector<const park::PoolSquare*> todo { &first }; !todo.empty();)
            {
                const park::PoolSquare& s = *todo.back();
                todo.pop_back();
                if (s.kind == park::PoolKind::Water)
                    complex.water.push_back(middle(s.col, s.row, static_cast<float>(s.surface) + park::kPoolBrim));
                else if (s.kind == park::PoolKind::Jacuzzi)
                    complex.spa.push_back(middle(s.col, s.row, static_cast<float>(s.surface) + kTuning.pools.spa_water));
                else if (s.kind == park::PoolKind::Tile || s.kind == park::PoolKind::Path) // Not a diving board's, a slope or stairs
                    complex.paved.push_back(middle(s.col, s.row, static_cast<float>(s.surface)));
                for (const auto [dc, dr] : kSides)
                    if (const auto next = squares.find({ s.col + dc, s.row + dr }); next != squares.end() && complex_of.try_emplace(next->first, complexes.size() - 1).second)
                        todo.push_back(next->second);
            }
        }
        // shortcut: swimmers stand at an extra, on its square, as on any paving; upgrade to lying on loungers and
        // going down slides with their motions. High diving boards go unused until the swimmers can climb and dive.
        for (const park::PoolExtra& extra : pools.extras)
            if (const auto square = squares.find({ extra.col, extra.row }); square != squares.end() && square->second->kind == park::PoolKind::Tile)
                complexes[complex_of.at(square->first)].extras.push_back(middle(extra.col, extra.row, static_cast<float>(square->second->surface)));
        // The rooms there were, by tile, and the complexes they opened on.
        std::map<std::pair<std::int32_t, std::int32_t>, entt::entity> rooms;
        for (const auto [e, door] : r.view<const PoolDoor>().each())
            rooms.emplace(std::pair { door.col, door.row }, e);
        std::vector<entt::entity> pool_entities(complexes.size(), entt::null);
        std::vector<entt::entity> kept;
        for (const park::PoolRoom& room : pools.rooms)
        {
            const auto [dc, dr] = kSides[static_cast<std::size_t>(room.turn & 3)];
            const auto square = squares.find({ room.col + dc, room.row + dr });
            if (square == squares.end())
                continue;
            const std::size_t complex = complex_of.at(square->first);
            const auto was = rooms.find({ room.col, room.row });
            const entt::entity e = was != rooms.end() ? was->second : r.create();
            kept.push_back(e);
            entt::entity& pool = pool_entities[complex];
            if (pool == entt::null && was != rooms.end() && r.valid(r.get<PoolDoor>(e).pool) && !std::ranges::contains(pool_entities, r.get<PoolDoor>(e).pool))
                pool = r.get<PoolDoor>(e).pool;
            if (pool == entt::null)
                pool = r.create();
            const auto surface = static_cast<float>(square->second->surface);
            // The door opens on the path tile on the room's side away from the pool.
            const glm::vec3 outside = middle(room.col - dc, room.row - dr, surface);
            const auto node = paths.nearest(outside);
            const bool on_path = node && !paths.nodes()[*node].removed && !paths.nodes()[*node].queue && std::abs(paths.nodes()[*node].height - outside.y) <= paths.level_height;
            auto& attraction = r.get_or_emplace<Attraction>(e);
            attraction.kind = AttractionKind::Pool;
            attraction.entrance = on_path ? *node : kNoNode;
            attraction.price = { room.ticket };
            attraction.open = room.open;
            r.get_or_emplace<PoolTank>(e).fluid = static_cast<PoolFluid>(std::min<std::uint8_t>(room.fluid, 2));
            r.emplace_or_replace<PoolDoor>(e, PoolDoor { .pool = pool, .col = room.col, .row = room.row, .room = middle(room.col, room.row, surface), .poolside = middle(square->second->col, square->second->row, surface) });
        }
        // A complex kept keeps its dirt.
        for (std::size_t c = 0; c < complexes.size(); ++c)
            if (pool_entities[c] != entt::null)
            {
                if (const auto* was = r.try_get<SwimPool>(pool_entities[c]))
                    complexes[c].dirt = was->dirt;
                r.emplace_or_replace<SwimPool>(pool_entities[c], std::move(complexes[c]));
            }
        for (const auto& [tile, e] : rooms)
            if (!std::ranges::contains(kept, e))
                pull_down(world, e);
        const auto gone = r.view<const SwimPool>() | std::views::filter([&](entt::entity e) { return !std::ranges::contains(pool_entities, e); }) | std::ranges::to<std::vector>();
        r.destroy(gone.begin(), gone.end());
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
            const PathTileSpot& tile = world.paths.nodes()[entrance].tile;
            Shop shop = sale->shop;
            shop.counter = { static_cast<float>(stall.base.spot->col - tile.col), static_cast<float>(stall.base.spot->row - tile.row) };
            r.emplace<Shop>(e, shop);
            ++added;
        }
        return added;
    }

    void restock(SimWorld& world, const save::Stall& stall)
    {
        const auto sale = sale_of(stall);
        for (const auto [e, a] : world.registry.view<Attraction>().each())
            if (a.saved == stall.base.id && sale)
            {
                a.price = Money { sale->price };
                auto& shop = world.registry.get_or_emplace<Shop>(e);
                shop.product = sale->shop.product;
                shop.serve = sale->shop.serve;
            }
    }

    std::optional<float> launch_of(const save::Ride& ride)
    {
        using enum save::OperatingMode;
        constexpr std::array kLaunched { PoweredLaunch, MultiplePoweredLaunch, ReverseInclinedLaunch, UpwardsLaunch, FreefallDrop };
        if (!std::ranges::contains(kLaunched, ride.mode) || ride.options.launch_speed <= 0)
            return std::nullopt;
        return ride.options.launch_speed;
    }

    std::optional<Coaster> coaster_of(const park::TrackCourse& course, const save::Ride& ride)
    {
        if (!std::ranges::contains(course.kinds, park::CourseKind::Station))
            return std::nullopt;
        Coaster c;
        if (!course.closed)
        {
            // A track with two ends runs back and forth: how depends on the ride's mode, how long on its circuits.
            // In a mode that goes round, the track is unfinished, and its train runs on off the end.
            // shortcut: a drop tower's car is pushed up like a launched tower's, not hauled up; plain shuttle mode
            // and the other modes run no train; upgrade when a park has them.
            using enum save::OperatingMode;
            const int circuits = std::max(ride.options.circuits, 1);
            c.closed = false;
            switch (ride.mode)
            {
                case ContinuousCircuit:
                case PoweredLaunch:
                case Race:
                case ReverseCableLiftHill:
                    break;
                case ReverseInclinedLaunch:
                    c.shuttle = Shuttle::Back;
                    c.passes = 2 * circuits; // In through the station and back into it, each circuit
                    break;
                case MultiplePoweredLaunch:
                    c.shuttle = Shuttle::Swing;
                    // As counted in the original game: through the station eight times more than twice its circuits,
                    // which fits the runs timed there in two parks of two and three circuits.
                    c.passes = 2 * circuits + 8;
                    break;
                case UpwardsLaunch:
                case FreefallDrop:
                    c.shuttle = ride.mode == FreefallDrop ? Shuttle::Drop : Shuttle::Tower;
                    c.passes = circuits; // One fall back into the station, each circuit
                    break;
                default:
                    return std::nullopt;
            }
            if (c.shuttle != Shuttle::None && !launch_of(ride))
                return std::nullopt;
        }
        c.distance = course.distance;
        c.points = course.points;
        c.ups = course.ups;
        for (const glm::vec3& p : course.points)
            c.height.push_back(p.y);
        for (const park::CourseKind k : course.kinds)
        {
            using enum park::CourseKind;
            c.kinds.push_back(k == Chain || k == Cable ? Stretch::Chain : k == Station ? Stretch::Station : k == BlockBrake ? Stretch::BlockBrake : k == Launch ? Stretch::Launch : k == Brake ? Stretch::Brake : Stretch::Plain);
        }
        // A brake or block brake with no speed of its own, under 1 m/s, slows to the ride's for its kind.
        for (std::size_t i = 0; i < course.brake_speeds.size(); ++i)
        {
            const float speed = course.brake_speeds[i];
            const float ride_speed = i < c.kinds.size() && c.kinds[i] == Stretch::BlockBrake ? ride.options.block_brake_off_speed : ride.options.brake_speed;
            c.brake_speeds.push_back(speed < 0 ? -1.0f : speed >= 1 ? speed : ride_speed);
        }
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
        if (ride.options.fixed_speed > 0 && c.shuttle == Shuttle::None)
            c.fixed_speed = ride.options.fixed_speed;
        c.launch_speed = launch_of(ride);
        c.blocks = ride.options.block_sectioned;
        if (!ride.trains.empty() && ride.trains[0].length > 0)
            c.train_length = ride.trains[0].length;
        if (!ride.trains.empty() && !ride.trains[0].cars.empty())
            c.cars = static_cast<int>(ride.trains[0].cars.size());
        // Brakes hold each car over them back with the force the ride's file gives (every tracked ride the game
        // ships sets 5000 N), against the whole train's mass as the park keeps it.
        // shortcut: one brake force for every ride and a train built here weighs 500 kg a car, chosen by eye
        // from the saved ones, upgrade when the ride's and the cars' own files are read here.
        constexpr float kBrakeForce = 5000;
        constexpr float kBoostForce = 10000; ///< A block brake's push on each car, as every ride file gives it
        constexpr float kCarMass = 500;
        const float mass = !ride.trains.empty() && ride.trains[0].mass > 0 ? ride.trains[0].mass : kCarMass * static_cast<float>(c.cars);
        c.brake_pull = kBrakeForce * static_cast<float>(c.cars) / mass;
        c.boost_pull = kBoostForce * static_cast<float>(c.cars) / mass;
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
            if (coaster.shuttle != Shuttle::None)
            {
                // shortcut: a shuttle runs its first train only, and one saved away from the station heads back to it
                // on its next pass, upgrade if a park keeps more than one or its passes.
                train.at = std::clamp(saved.distance, std::min(train.train_length, length), length);
                train.travelled = std::abs(train.at - train.start);
                train.running = train.travelled > 0.5f;
                train.at = train.running ? train.at : train.start;
                train.travelled = train.running ? train.travelled : 0;
                train.passed = train.running ? train.passes : 0;
                train.speed = train.running ? saved.speed : 0;
                out.trains.push_back(train);
                break;
            }
            train.at = length > 0 ? std::fmod(std::fmod(saved.distance, length) + length, length) : 0;
            train.travelled = length > 0 ? std::fmod(train.at - train.start + length, length) : 0;
            constexpr float kAtStart = 0.5f; // Metres off the start that still count as standing at it
            if (train.travelled < kAtStart || train.travelled > length - kAtStart)
            {
                train.at = train.start;
                train.travelled = 0;
            }
            train.running = train.travelled > 0;
            train.next_stop = static_cast<std::size_t>(std::ranges::count_if(train.stops, [&](float stop) { return stop < train.travelled; }));
            train.launched = train.travelled >= train.train_length;
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
                world.registry.emplace<PathItem>(world.registry.create(), PathItem { .node = *at, .kind = *kind, .object = o.id });
                ++added;
            }
        }
        return added;
    }

    entt::entity import_gate(SimWorld& world, std::span<const save::PlacedObject> objects, const std::function<std::optional<std::array<std::int32_t, 2>>(const save::PlacedObject&)>& squares_of)
    {
        const PathNetwork& paths = world.paths;
        entt::entity gate = entt::null;
        EntrancePaths under;
        for (const save::PlacedObject& o : objects)
        {
            const auto squares = o.hidden ? std::nullopt : squares_of(o);
            if (!squares)
                continue;
            const auto at = path_by(paths, o);
            if (at && gate == entt::null)
            {
                gate = world.registry.create();
                world.registry.emplace<ParkGate>(gate, *at);
            }
            if (at)
                under.nodes.push_back(*at);
            // A quarter turn either way lays the entrance's X along the park's rows.
            const bool across = (o.rotation & 1) != 0;
            const std::int32_t span_col = (*squares)[across ? 1 : 0];
            const std::int32_t span_row = (*squares)[across ? 0 : 1];
            for (NodeId n = 0; n < paths.nodes().size(); ++n)
            {
                const PathNode& node = paths.nodes()[n];
                if (!node.removed && !node.queue && node.tile.col >= o.col && node.tile.col < o.col + span_col && node.tile.row >= o.row && node.tile.row < o.row + span_row && !std::ranges::contains(under.nodes, n))
                    under.nodes.push_back(n);
            }
        }
        world.registry.ctx().insert_or_assign(std::move(under));
        return gate;
    }

    void keep_staff_in(SimWorld& world, const save::TerrainGrid& grid)
    {
        for (NodeId n = 0; n < world.paths.nodes().size(); ++n)
            world.paths.keep_staff_off(n, kept_from_staff(world, grid, n));
    }

    std::size_t no_entry_side(const save::PlacedObject& sign)
    {
        // The front faces lower columns unturned, then a quarter clockwise each: higher rows, higher columns,
        // lower rows (park::turned_x); the network numbers the sides +col, +row, -col, -row.
        constexpr std::array<std::size_t, 4> kSide { 2, 1, 0, 3 };
        return kSide[static_cast<std::size_t>(((sign.rotation % 4) + 4) % 4)];
    }

    void import_party_mix(SimWorld& world, const save::GroupMix& mix)
    {
        // The save lists the kinds in PartyKind's order.
        world.registry.ctx().insert_or_assign(PartyMix { .weights = mix.weights, .single_parent = mix.single_parent });
    }

    Upkeep upkeep_of(const save::Ride& ride)
    {
        // The intervals the maintenance page lists, in its order: every 10, 20, 30 and 45 minutes, every hour,
        // every two hours, never.
        constexpr std::array<float, 7> kIntervals { 600, 1200, 1800, 2700, 3600, 7200, -1 };
        const std::int32_t interval = ride.breakdown.inspection_interval;
        // Broken by its flag alone: saves keep a breakdown kind on most open, running rides too, and a repair
        // status on some fixed ones.
        const bool broken = (ride.base.flags & save::AttractionCommon::kBrokenDown) != 0;
        return {
            .reliability = { std::clamp(ride.breakdown.reliability / 100, 0.0f, 1.0f) },
            .broken = broken,
            .since_inspection = { std::max(ride.base.time_since_inspection, 0.0f) },
            .inspect_every = interval >= 0 && std::cmp_less(interval, kIntervals.size()) ? Seconds { kIntervals[static_cast<std::size_t>(interval)] } : Upkeep { }.inspect_every,
            .kind = broken ? static_cast<Breakdown>(std::to_underlying(ride.breakdown.current)) : Breakdown::None,
            .last = static_cast<Breakdown>(std::to_underlying(ride.breakdown.last)),
            .downtime = ride.breakdown.downtime_windows,
        };
    }

    std::vector<entt::entity> import_rides(SimWorld& world, const save::RideModel& rides, std::span<const std::optional<RideTiming>> flat_timings, std::span<const std::optional<Coaster>> coasters, std::span<const std::optional<std::uint32_t>> guests)
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
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size() / std::max<std::size_t>(coaster ? ride.trains.size() : 1, 1)), .run = ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0 };
            else if (ride.kind == save::RideKind::Tracked && !ride.seats.empty() && (ride.mode == save::OperatingMode::Boat || ride.mode == save::OperatingMode::WaterTransport))
                // shortcut: boats that sail off their dock go out all at once and come back after a set trip, with
                // no boat moving on the water; upgrade with boats steering over the lake.
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size()), .run = kTuning.boats.trip.value };
            // Rides left out still make room in the park while open and working.
            const auto elsewhere = [&] {
                if (i < guests.size() && guests[i] && ride.base.state == save::AttractionState::Open && (ride.base.flags & save::AttractionCommon::kBrokenDown) == 0)
                    world.registry.ctx().emplace<RoomElsewhere>().guests += *guests[i];
            };
            // Tested or open, a tracked ride's trains run its track without doors or paths, round a whole circuit, a
            // shuttle's track, or an unfinished one to its end, as a test run needs neither; guests come once its doors
            // are on the paths (relink_attractions).
            const bool runs_alone = coaster != nullptr && ride.base.state != save::AttractionState::Closed;
            auto links = timing && !ride.stations.empty() && ride.stations.front().entrance_spot ? ride_links(world.paths, ride.stations.front()) : std::nullopt;
            if (!timing || (!links && !runs_alone))
            {
                elsewhere();
                continue;
            }
            const NodeId entrance = links ? links->entrance : kNoNode;
            const NodeId exit = links ? links->exit : kNoNode;
            std::vector<NodeId> line = links ? std::move(links->line) : std::vector<NodeId> { };
            auto& r = world.registry;
            const entt::entity e = r.create();
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = entrance, .price = ride.ticket_price, .open = ride.base.state == save::AttractionState::Open, .testing = ride.base.state == save::AttractionState::Testing, .saved = ride.base.id });
            // Rated as the park has it; else from its last test, or a test run of our own.
            std::optional<Ratings> rated;
            if (ride.assessment.valid)
                rated = Ratings { ride.assessment.excitement, ride.assessment.intensity, ride.assessment.nausea };
            else if (ride.stats.last_lap)
                rated = rate(world.ratings, ride.base.symbol, *ride.stats.last_lap);
            else if (const auto test = coaster ? test_lap(*coaster) : std::nullopt)
                rated = rate(world.ratings, ride.base.symbol, *test);
            CycleRide cycle { .capacity = timing->seats, .start = { timing->start }, .run = { timing->run }, .stop = { timing->stop }, .loops = timing->loops, .line = std::move(line), .exit = exit };
            if (rated)
            {
                cycle.excitement.value = rating(rated->excitement);
                cycle.intensity.value = rating(rated->intensity);
                cycle.nausea.value = rating(rated->nausea);
            }
            if (ride.assessment.valid)
                cycle.cover.value = std::clamp(ride.assessment.under_cover, 0.0f, 1.0f);
            cycle.splash = splashes(ride.base.symbol);
            if (i < guests.size() && guests[i])
                cycle.room = *guests[i];
            std::optional<Coaster> stopping = coaster != nullptr ? std::optional(*coaster) : std::nullopt;
            // A circuit with more than one station: its trains set off from the first station and stop at each of
            // the others in turn, each found as the run of station track nearest its entrance.
            if (stopping && stopping->shuttle == Shuttle::None && stopping->closed && ride.stations.size() > 1 && stopping->points.size() == stopping->kinds.size())
            {
                struct Run
                {
                    float end = -1; ///< Along the track, where a train waits at its far end; -1 for one round the track's end
                    std::vector<glm::vec3> points { };
                };
                std::vector<Run> runs;
                const auto& kinds = stopping->kinds;
                for (std::size_t k = 0; k < kinds.size(); ++k)
                {
                    if (kinds[k] != Stretch::Station)
                        continue;
                    if (k == 0 || kinds[k - 1] != Stretch::Station)
                        runs.emplace_back();
                    runs.back().points.push_back(stopping->points[k]);
                    if (k + 1 < kinds.size() && kinds[k + 1] != Stretch::Station)
                        runs.back().end = stopping->distance[k + 1];
                }
                const auto run_of = [&](const save::Station& station) -> std::optional<std::size_t> {
                    if (!station.entrance_spot)
                        return std::nullopt;
                    const glm::vec2 at = world.paths.origin + (glm::vec2(station.entrance_spot->col, station.entrance_spot->row) + 0.5f) * world.paths.tile_size;
                    std::optional<std::size_t> best;
                    float nearest = std::numeric_limits<float>::max();
                    for (std::size_t k = 0; k < runs.size(); ++k)
                        for (const glm::vec3& p : runs[k].points)
                            if (const float d = glm::distance(at, glm::vec2(p.x, p.z)); d < nearest)
                            {
                                nearest = d;
                                best = k;
                            }
                    return best && runs[*best].end >= 0 ? best : std::nullopt;
                };
                if (const auto first = run_of(ride.stations.front()))
                {
                    const float start = runs[*first].end;
                    const float length = stopping->length();
                    std::vector<std::pair<float, Stop>> others;
                    for (std::size_t k = 1; k < ride.stations.size(); ++k)
                    {
                        // A ride whose first station no path reaches takes guests at none (relink_attractions)
                        const auto run = run_of(ride.stations[k]);
                        auto station = links && run && *run != *first ? ride_links(world.paths, ride.stations[k]) : std::nullopt;
                        // shortcut: a train runs past a station no path reaches, upgrade if it should stop there empty
                        if (!station)
                            continue;
                        const float on = std::fmod(runs[*run].end - start + length, length);
                        if (std::ranges::none_of(others, [&](const auto& o) { return o.first == on; }))
                            others.push_back({ on, Stop { .entrance = station->entrance, .line = std::move(station->line), .exit = station->exit, .saved = k } });
                    }
                    std::ranges::sort(others, { }, &std::pair<float, Stop>::first);
                    stopping->start = start;
                    stopping->at = start;
                    for (auto& [on, stop] : others)
                    {
                        stopping->stops.push_back(on);
                        cycle.stops.push_back(std::move(stop));
                    }
                }
            }
            r.emplace<CycleRide>(e, std::move(cycle));
            if (stopping)
                r.emplace<Trains>(e, trains_of(*stopping, ride));
            if (ride.base.time_since_crash >= 0)
                r.emplace<Wrecks>(e, Wrecks { .since = { ride.base.time_since_crash } });
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

    void import_boats(SimWorld& world, const save::RideModel& rides, std::span<const entt::entity> added, std::span<const std::optional<park::TrackCourse>> courses, const save::TerrainGrid& grid, std::span<const save::WaterBody> water)
    {
        auto& r = world.registry;
        for (std::size_t i = 0; i < std::min({ rides.rides.size(), added.size(), courses.size() }); ++i)
        {
            const save::Ride& ride = rides.rides[i];
            const auto& course = courses[i];
            if ((ride.mode != save::OperatingMode::Boat && ride.mode != save::OperatingMode::WaterTransport) || !r.valid(added[i]) || r.all_of<Trains>(added[i]) || !course || course->points.empty() || ride.trains.empty())
                continue;
            if (!r.ctx().contains<WaterTiles>())
                r.ctx().emplace<WaterTiles>(water_tiles(grid, water));
            const glm::vec3 leave = course->points.back();
            const glm::vec3 heading = park::course_direction(*course, course->length());
            Boats boats { .dock = { .leave = { leave.x, leave.z }, .heading = { heading.x, heading.z }, .enter = { course->points.front().x, course->points.front().z } } };
            boats.roams = ride.mode == save::OperatingMode::Boat;
            for (const glm::vec3& stop : course->other_stations)
                boats.calls.emplace_back(stop.x, stop.z);
            boats.boats.resize(ride.trains.size());
            for (Boat& boat : boats.boats)
                boat.at = boats.dock.enter;
            r.emplace<Boats>(added[i], std::move(boats));
        }
    }

    void link_stations(SimWorld& world, std::span<const entt::entity> added, std::span<const std::optional<park::TrackCourse>> courses, const save::TerrainGrid& grid)
    {
        if (grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return;
        constexpr float kSameLevel = 0.5f; // Metres apart that still count as one level
        struct Tile
        {
            int col = 0;
            int row = 0;
            float up = 0;
        };
        // The tiles of each ride's stations, by the middle of each stretch of them.
        const std::size_t count = std::min(added.size(), courses.size());
        std::vector<std::vector<Tile>> stations(count);
        auto& r = world.registry;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& course = courses[i];
            if (!r.valid(added[i]) || !course)
                continue;
            for (std::size_t k = 0; k + 1 < course->kinds.size() && k + 1 < course->points.size(); ++k)
            {
                if (course->kinds[k] != park::CourseKind::Station)
                    continue;
                const glm::vec3 middle = (course->points[k] + course->points[k + 1]) / 2.0f;
                stations[i].push_back({ static_cast<int>(std::floor((middle.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((middle.z - grid.origin_z) / grid.tile_size_z)), middle.y });
            }
        }
        for (std::size_t i = 0; i < count; ++i)
            for (std::size_t j = i + 1; j < count; ++j)
            {
                const bool beside = std::ranges::any_of(stations[i], [&](const Tile& a) { return std::ranges::any_of(stations[j], [&](const Tile& b) { return std::abs(a.col - b.col) + std::abs(a.row - b.row) == 1 && std::abs(a.up - b.up) < kSameLevel; }); });
                if (!beside)
                    continue;
                r.get_or_emplace<StationNeighbours>(added[i]).rides.push_back(added[j]);
                r.get_or_emplace<StationNeighbours>(added[j]).rides.push_back(added[i]);
            }
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
        std::unordered_map<std::uint64_t, const save::PlacedObject*> objects;
        for (const save::PlacedObject& o : placed)
            objects.emplace(o.id, &o);
        auto& r = world.registry;
        std::size_t added = 0;
        const auto add = [&](std::uint64_t object, PathItemKind kind) -> entt::entity {
            const auto found = objects.find(object);
            const entt::entity e = found == objects.end() ? entt::null : add_path_item(world, *found->second, kind);
            added += e != entt::null ? 1 : 0;
            return e;
        };
        for (const save::BinState& b : bins)
            if (const entt::entity e = add(b.object_ref, PathItemKind::Bin); e != entt::null)
                r.replace<Bin>(e, Bin { b.capacity / kSavedBinPiece, std::round(b.contents / kSavedBinPiece) });
        for (const std::uint64_t bench : benches)
            add(bench, PathItemKind::Bench);
        for (const save::SignState& s : signs)
            if (const entt::entity e = add(s.object_ref, PathItemKind::Sign); e != entt::null && s.kind == 1)
                world.paths.set_no_entry(r.get<PathItem>(e).node, no_entry_side(*objects.at(s.object_ref)), true);
        return added;
    }

    void set_wall(SimWorld& world, const park::EdgeWall& wall, bool standing)
    {
        // path_decks counts the sides from +row, the network from +col: the same sides, each pair swapped.
        const std::size_t side = wall.side ^ 1U;
        if (standing)
            world.paths.add_wall(wall.tile.col, wall.tile.row, side, wall.low, wall.high);
        else
            world.paths.remove_wall(wall.tile.col, wall.tile.row, side, wall.low, wall.high);
    }

    entt::entity add_path_item(SimWorld& world, const save::PlacedObject& o, PathItemKind kind)
    {
        const PathNetwork& paths = world.paths;
        if (o.hidden)
            return entt::null;
        const glm::vec3 middle { paths.origin.x + (static_cast<float>(o.col) + 0.5f) * paths.tile_size, static_cast<float>(o.base_height.value_or(0)) * paths.level_height, paths.origin.y + (static_cast<float>(o.row) + 0.5f) * paths.tile_size };
        const auto at = paths.nearest(middle);
        if (!at)
            return entt::null;
        auto& r = world.registry;
        const entt::entity e = r.create();
        r.emplace<PathItem>(e, PathItem { .node = *at, .kind = kind, .object = o.id });
        if (kind == PathItemKind::Bin)
            r.emplace<Bin>(e, Bin { kNewBinPieces, 0 });
        return e;
    }

    std::vector<entt::entity> import_staff(SimWorld& world, const save::PeopleModel& people)
    {
        auto& r = world.registry;
        std::vector<entt::entity> out(people.staff.size(), entt::null);
        for (std::size_t i = 0; i < people.staff.size(); ++i)
        {
            const save::Person& p = people.staff[i].person;
            const glm::vec3 position { p.position.x, p.position.y, p.position.z };
            using enum save::StaffKind;
            const save::StaffKind kind = people.staff[i].kind;
            const StaffRole role = kind == Handyman ? StaffRole::Handyman : kind == Mechanic ? StaffRole::Mechanic : kind == SecurityGuard ? StaffRole::Guard : kind == Vendor ? StaffRole::Vendor : kind == AnimalKeeper ? StaffRole::Keeper : StaffRole::Other;
            // A vendor stays in their booth, a keeper in their enclosure (import_animal_houses gives them their work).
            const bool walks = role != StaffRole::Vendor && role != StaffRole::Keeper;
            auto at = world.paths.nearest(position);
            if (!at || world.paths.nodes()[*at].queue)
                at = walkway_near(world.paths, position);
            if (!at && walks)
                continue;
            const entt::entity e = r.create();
            const save::Staff& saved = people.staff[i];
            Staff staff { .role = role, .tasks = saved.task_flags, .training = std::clamp(saved.training, 0, kMostTraining), .warnings = saved.warnings, .happiness = std::clamp(saved.happiness, 0.0f, 1.0f), .laziness = std::clamp(saved.laziness, 0.0f, 1.0f) };
            set_wage_level(staff, saved.wage_level);
            r.emplace<Staff>(e, staff);
            r.emplace<Pose>(e, position, -p.heading, position, -p.heading); // The original turns the other way
            if (walks)
                start_walking(world, e, *at);
            out[i] = e;
        }
        return out;
    }

    entt::entity add_vendor(SimWorld& world, glm::vec3 at)
    {
        auto& r = world.registry;
        const entt::entity e = r.create();
        Staff staff { .role = StaffRole::Vendor };
        set_wage_level(staff, staff.wage_level);
        r.emplace<Staff>(e, staff);
        r.emplace<Pose>(e, Pose { .position = at, .was = at });
        return e;
    }

    Departure departure_of(const save::OperatingOptions& options)
    {
        const bool listed = options.wait_for_load >= 0 && options.wait_for_load <= static_cast<std::int32_t>(LoadWait::Any);
        return { .wait_for = listed ? static_cast<LoadWait>(options.wait_for_load) : LoadWait::Any, .min_wait = std::max(options.min_wait, -1), .max_wait = std::max(options.max_wait, -1), .leave_on_arrival = options.leave_if_other_train_enters_station, .sync = options.sync_trains };
    }

    std::size_t board_saved_riders(SimWorld& world, const save::PeopleModel& people, std::span<const entt::entity> guests, const save::RideModel& saved, std::span<const entt::entity> rides)
    {
        auto& r = world.registry;
        std::unordered_map<save::ObjectId, entt::entity> ride_of;
        // Not a ride run with no doors on the paths, as its riders would have nowhere to get off.
        for (std::size_t i = 0; i < saved.rides.size() && i < rides.size(); ++i)
            if (rides[i] != entt::null && r.get<Attraction>(rides[i]).entrance != kNoNode)
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
                // Saved riding, so it is under way; from the start of its run, as the save does not say how far.
                if (cycle.phase == RidePhase::Loading)
                    cycle.phase = RidePhase::Running;
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
        auto guests = view | std::ranges::to<std::vector<entt::entity>>();
        std::ranges::sort(guests);
        // Each queue tile of a ride's lines, by node: the ride and how far the tile is from the front, and the
        // station's line it is on.
        std::unordered_map<NodeId, std::pair<entt::entity, std::size_t>> lines;
        std::unordered_map<NodeId, std::uint8_t> stations;
        const auto ride_view = r.view<const CycleRide>();
        for (const entt::entity e : ride_view)
        {
            const auto& cycle = r.get<CycleRide>(e);
            for (std::size_t station = 0; station <= cycle.stops.size(); ++station)
            {
                const auto& line = station == 0 ? cycle.line : cycle.stops[station - 1].line;
                for (std::size_t k = 0; k < line.size(); ++k)
                    if (lines.emplace(line[k], std::pair { e, k }).second)
                        stations.emplace(line[k], static_cast<std::uint8_t>(station));
            }
        }
        std::vector<std::pair<std::size_t, entt::entity>> waiting; // By tile from the front, then guest
        std::size_t started = 0;
        for (const entt::entity e : guests)
        {
            const glm::vec3 position = r.get<Pose>(e).position;
            auto at = world.paths.nearest(position);
            if (!at || (world.paths.nodes()[*at].queue && !lines.contains(*at)))
                at = walkway_near(world.paths, position);
            if (!at)
                continue;
            const auto on = lines.find(*at);
            if (on == lines.end())
                start_walking(world, e, *at);
            else
            {
                start_walking(world, e, *at);
                auto& stride = r.get<Stride>(e);
                stride.along.value = static_cast<float>(on->second.second);
                r.get<Errand>(e) = { .kind = ErrandKind::Queueing, .target = on->second.first, .station = stations.at(*at) };
                waiting.emplace_back(on->second.second, e);
            }
            ++started;
        }
        // Those nearer the front were there first.
        std::ranges::stable_sort(waiting, { }, &std::pair<std::size_t, entt::entity>::first);
        for (const auto& [_, e] : waiting)
        {
            const Errand& errand = r.get<Errand>(e);
            auto& cycle = r.get<CycleRide>(errand.target);
            (errand.station == 0 ? cycle.queue : cycle.stops[errand.station - 1].queue).push_back(e);
        }
        return started;
    }

} // namespace rct3::simulation
