// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "ovl/animated_ride.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/object/placement.hpp"
#include "park/path/decks.hpp"
#include "park/path/furniture.hpp"
#include "park/queue_fences.hpp"
#include "park/terrain_mesh.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/state.hpp"
#include "simulation/ratings.hpp"
#include "simulation/staff_care.hpp"
#include "simulation/swimming.hpp"
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

        /// Drink, food or umbrella, guessed from a name, for items whose kind is unknown or neither feeds nor quenches.
        // shortcut: matches words in item names and kind symbols (Umbrella:cid), upgrade when carried items' kind flags are read.
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
        /// Its shop's menu holds every such item, each with the extras of `kinds` that go on it.
        struct Sale
        {
            Shop shop;
            std::int64_t price = 0;
        };
        std::optional<Sale> sale_of(const save::model::Stall& stall, const rct3::park::CarriedKinds& kinds)
        {
            const auto kind_of = [](std::span<const rct3::park::CarriedKind> of, std::string_view symbol) {
                const auto it = std::ranges::find_if(of, [&](const rct3::park::CarriedKind& k) { return strings::equals_ignoring_case(k.symbol, symbol); });
                return it == of.end() ? nullptr : &*it;
            };
            std::optional<Sale> sale;
            std::vector<Ware> menu;
            for (const save::model::StallItem& item : stall.items)
            {
                const std::string_view name = product_named(item.name) ? std::string_view(item.name) : std::string_view(item.kind_symbol);
                const rct3::park::CarriedKind* kind = kind_of(kinds.items, item.kind_symbol);
                // What it feeds or quenches says what it is; a name only where its kind is unknown or does neither.
                const auto product = kind && kind->effects[0] > 0 ? std::optional(Product::Food) : kind && kind->effects[1] > 0 ? std::optional(Product::Drink) : product_named(name);
                if (!product)
                    continue;
                Ware& ware = menu.emplace_back(Ware { *product, serve_named(name), { item.price }, item.kind_symbol });
                // An extra goes on when the stall offers it and it is ticked on the item, or the guest picks.
                if (kind)
                {
                    ware.extras = static_cast<std::uint8_t>(std::min<std::size_t>(kind->extras.size(), 32));
                    for (std::size_t i = 0; i < ware.extras; ++i)
                    {
                        const auto offered = std::ranges::find_if(stall.extras, [&](const save::model::StallExtra& e) { return strings::equals_ignoring_case(e.kind_symbol, kind->extras[i]); });
                        const std::int32_t level = offered == stall.extras.end() ? save::model::StallExtra::kNone : offered->level;
                        if (level <= save::model::StallExtra::kNone || std::cmp_greater_equal(level, kTuning.eating.extra_amounts.size()) || (level != save::model::StallExtra::kGuestChoice && (item.extras >> i & 1) == 0))
                            continue;
                        ++ware.topped;
                        if (const rct3::park::CarriedKind* extra = kind_of(kinds.extras, kind->extras[i]))
                        {
                            const float amount = kTuning.eating.extra_amounts[static_cast<std::size_t>(level)];
                            for (std::size_t e = 0; e < ware.topping.size(); ++e)
                                ware.topping[e] += extra->effects[e] * amount;
                            ware.cost.value += std::llround(static_cast<double>(extra->cost) * amount);
                        }
                    }
                    // What the extras cost the stall, guests will pay the more for; an item with no worth of its
                    // own keeps none.
                    for (std::size_t w = 0; w < ware.worth.size(); ++w)
                        ware.worth[w].value = kind->worth[w] > 0 ? kind->worth[w] + ware.cost.value : 0;
                    ware.cost.value += kind->cost;
                }
                if (!sale)
                    sale = Sale { { *product, serve_named(name) }, item.price };
            }
            if (const auto product = product_named(stall.base.symbol); !sale && product && !stall.items.empty())
                sale = Sale { { *product, serve_named(stall.base.symbol) }, stall.items.front().price };
            if (sale)
                sale->shop.menu = std::move(menu);
            return sale;
        }
        /// The footpath node on the tile `o` stands on, or on one beside it.
        std::optional<NodeId> path_by(const PathNetwork& paths, const save::model::PlacedObject& o)
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
        bool kept_from_staff(const SimWorld& world, const save::model::TerrainGrid& grid, NodeId n)
        {
            const PathTileSpot& t = world.paths.nodes()[n].tile;
            // Without terrain the whole park is its own.
            if (!grid.empty() && (!grid.contains(t.col, t.row) || (grid.at(t.col, t.row).ownership() != save::model::Ownership::Owned && grid.at(t.col, t.row).ownership() != save::model::Ownership::OwnedRights)))
                return true;
            const auto* under = world.registry.ctx().find<EntrancePaths>();
            return under != nullptr && std::ranges::contains(under->nodes, n);
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
        std::pair<PathTileSpot, float> node_spot(const PathNetwork& paths, const save::model::PathTile& p, const save::model::TerrainGrid& grid)
        {
            float height = 0;
            if (p.elevation && p.elevation->slope != save::model::kSlopeOnGround)
            {
                // The stored height is at one end of a sloped piece; its middle is half the rise up or down.
                const float rise = rct3::park::ground::path_rise(p.elevation->slope);
                height = static_cast<float>(p.elevation->quantised_height) + (rct3::park::slope_falls(p.elevation->slope) ? -rise : rise) / 2;
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

        /// The path node on the tile next to `spot` the way `way` (a step of turned_x), nearest its height and no
        /// further off than a path that sets a paving's style; queue tiles only with `queues`, otherwise footpaths
        /// only. kNoNode when none: a ride door or a stall joins only the path on the side it opens to, not one
        /// beside it.
        NodeId path_at(const PathNetwork& paths, const save::model::PlacementSpot& spot, glm::vec2 way, bool queues)
        {
            const int col = spot.col + static_cast<int>(way.x);
            const int row = spot.row + static_cast<int>(way.y);
            NodeId found = kNoNode;
            int gap = 0;
            for (NodeId n = 0; n < paths.nodes().size(); ++n)
            {
                const PathNode& node = paths.nodes()[n];
                const int g = std::abs(node.tile.level - spot.height);
                if (node.removed || node.queue != queues || node.tile.col != col || node.tile.row != row || static_cast<float>(g) > rct3::park::ground::kPavingReach)
                    continue;
                if (found == kNoNode || g < gap)
                {
                    found = n;
                    gap = g;
                }
            }
            return found;
        }

        /// The path behind the ride door `door`, on the tile away from the ride.
        NodeId behind_door(const PathNetwork& paths, const save::model::PlacementSpot& door, bool queues)
        {
            return path_at(paths, door, rct3::park::object::turned_x(rct3::park::ModelTurn::from(door.direction).turned(2)), queues);
        }

        /// The path a facility such as a toilet opens onto: it opens onto every side, so the first side with a path.
        // shortcut: guests go in from the first side found with a path, upgrade if a facility ever needs several ways in.
        NodeId facility_door(const PathNetwork& paths, const save::model::PlacementSpot& spot)
        {
            NodeId entrance = kNoNode;
            for (std::int32_t side = 0; side < 4 && entrance == kNoNode; ++side)
                entrance = path_at(paths, spot, rct3::park::object::turned_x(rct3::park::ModelTurn { side }), false);
            return entrance;
        }

        /// The path in front of a stall facing `facing`, on the side it serves; facings point at -row, -col, +row,
        /// +col.
        NodeId stall_front(const PathNetwork& paths, const save::model::PlacementSpot& spot, std::int32_t facing)
        {
            return path_at(paths, spot, rct3::park::object::turned_x(rct3::park::ModelTurn::from(facing).turned(3)), false);
        }

        /// Guests line up on the queue that reaches the entrance, or walk straight in from a path; none when
        /// neither reaches it.
        std::optional<RideLinks> ride_links(const PathNetwork& paths, const save::model::Station& station)
        {
            if (!station.entrance_spot)
                return std::nullopt;
            const save::model::PlacementSpot& at = *station.entrance_spot;
            RideLinks links;
            if (const NodeId front = behind_door(paths, at, true); front != kNoNode && station.queue_line != 0 && paths.nodes()[front].line == station.queue_line)
                links.line = queue_line(paths, front);
            links.entrance = links.line.empty() ? behind_door(paths, at, false) : links.line.back();
            if (links.entrance == kNoNode)
                return std::nullopt;
            if (station.exit_spot)
                links.exit = behind_door(paths, *station.exit_spot, false);
            return links;
        }

        /// Of `platforms`, the one with track nearest `station`'s entrance, its points taken by `points`; none
        /// for a station without one.
        std::optional<std::size_t> nearest_platform(const PathNetwork& paths, const save::model::Station& station, const auto& platforms, auto points)
        {
            if (!station.entrance_spot)
                return std::nullopt;
            const glm::vec2 at = paths.origin + (glm::vec2(station.entrance_spot->col, station.entrance_spot->row) + 0.5f) * paths.tile_size;
            std::optional<std::size_t> best;
            float nearest = std::numeric_limits<float>::max();
            for (std::size_t k = 0; k < platforms.size(); ++k)
                for (const glm::vec3& p : std::invoke(points, platforms[k]))
                    if (const float d = glm::distance(at, glm::vec2(p.x, p.z)); d < nearest)
                    {
                        nearest = d;
                        best = k;
                    }
            return best;
        }

        /// The path node, not a queue, nearest `at` anywhere in the park.
        // shortcut: guests saved on a ride we do not bring over or in its queue step out to the nearest path, as
        // nothing moves them there; upgrade when those rides are simulated.
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

    void stage_show(CycleRide& ride, const save::model::RideShow& show)
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

    std::vector<std::optional<RideTiming>> flat_ride_timings(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, const std::function<const rct3::park::object::ObjectModel*(const save::model::PlacedObject&)>& model_of)
    {
        std::vector<std::optional<RideTiming>> out(rides.rides.size());
        for (std::size_t i = 0; i < rides.rides.size(); ++i)
        {
            const save::model::Ride& ride = rides.rides[i];
            if (ride.kind != save::model::RideKind::Flat || ride.base.placement == 0)
                continue;
            // The ride stands as the objects hanging from its placement; the one with seats is the ride itself.
            for (const save::model::PlacedObject& o : objects)
            {
                if (o.owner_ref != ride.base.placement)
                    continue;
                if (const rct3::park::object::ObjectModel* model = model_of(o))
                    if ((out[i] = flat_ride_timing(model->animation_seconds, model->seats, ride.loop_count)))
                        break;
            }
        }
        return out;
    }

    namespace
    {
        /// What `pick` reads from each ride's item in its overlay, by the ride's place in `rides.rides`.
        template<typename Pick> std::vector<std::optional<std::uint32_t>> from_ride_items(const save::model::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find, Pick pick)
        {
            // Each overlay's rides by symbol (tag included), read once.
            std::unordered_map<std::string, std::map<std::string, std::uint32_t, strings::LessIgnoringCase>> loaded;
            std::vector<std::optional<std::uint32_t>> out(rides.rides.size());
            for (std::size_t i = 0; i < rides.rides.size(); ++i)
            {
                const save::model::Ride& ride = rides.rides[i];
                auto [at, added] = loaded.try_emplace(ride.base.overlay_path);
                const auto file = added ? find(std::format("{}.common.ovl", ride.base.overlay_path)) : std::nullopt;
                if (auto overlay = file ? ovl::Overlay::load(*file) : std::unexpected(ovl::Error::MissingFile))
                {
                    for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
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

    std::vector<std::optional<std::uint32_t>> ride_guests(const save::model::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) { return ride.guests; });
    }

    std::vector<std::optional<std::uint32_t>> ride_kinds(const save::model::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) { return std::optional(ride.info.type_id); });
    }

    std::vector<std::optional<std::uint32_t>> ride_seatings(const save::model::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find)
    {
        return from_ride_items(rides, find, [](const auto& ride) -> std::optional<std::uint32_t> {
            if constexpr (requires { ride.seating; })
                return ride.seating;
            return std::nullopt;
        });
    }

    std::size_t import_cover(SimWorld& world, const std::function<std::vector<rct3::park::Overhead>(std::span<const glm::vec3>)>& overhead)
    {
        PathNetwork& paths = world.paths;
        std::vector<glm::vec3> middles;
        for (NodeId n = 0; n < paths.nodes().size(); ++n)
            middles.push_back(paths.centre(n));
        const std::vector<rct3::park::Overhead> over = overhead(middles);
        std::size_t roofed = 0;
        for (NodeId n = 0; n < paths.nodes().size() && n < over.size(); ++n)
        {
            paths.cover(n, over[n].roof, over[n].shade);
            roofed += over[n].roof ? 1 : 0;
        }
        return roofed;
    }

    void import_paths(SimWorld& world, std::span<const save::model::PathTile> tiles, const save::model::TerrainGrid& grid)
    {
        PathNetwork& paths = world.paths;
        paths.tile_size = grid.tile_size_x > 0 ? grid.tile_size_x : paths.tile_size;
        paths.origin = { grid.origin_x, grid.origin_z };
        paths.level_height = 1;
        // A steep slope climbs two units across a tile, and so may the ground under paths laid on it.
        // shortcut: joins tiles by the levels at their middles, upgrade to matching the heights of shared edges
        // if stacked paths two units apart ever join wrongly.
        paths.max_step = 2;
        for (const save::model::PathTile& p : tiles)
            (void)add_path(world, p, grid);
    }

    NodeId add_path(SimWorld& world, const save::model::PathTile& path, const save::model::TerrainGrid& grid)
    {
        if (path.kind == save::model::PathKind::Unknown)
            return kNoNode;
        const auto [spot, height] = node_spot(world.paths, path, grid);
        const std::uint64_t line = path.queue ? path.queue->queue_line_ref : 0;
        const NodeId id = world.paths.add(spot, path.kind == save::model::PathKind::Queue, height, line);
        world.paths.set_line(id, line);
        // Its surface: the ground's own corners under a path on the ground, a ramp from the height of the edge behind
        // it to that of the edge it faces, else flat.
        std::array<float, 4> corners { height, height, height, height };
        if ((rct3::park::ground::is_ground_queue(path) || (!path.elevation && path.kind == save::model::PathKind::Ground)) && grid.contains(path.col, path.row))
            corners = grid.at(path.col, path.row).corners;
        else if (path.elevation && rct3::park::ground::path_rise(path.elevation->slope) > 0)
        {
            // A ramp without a stored heading meets no edge ahead, so it keeps its flat corners.
            if (const auto ahead = rct3::park::path::stored_heading(path.direction))
            {
                const rct3::park::Side behind = ahead->opposite();
                if (const auto up = rct3::park::path::edge_height(path, grid, *ahead), down = rct3::park::path::edge_height(path, grid, behind); up && down)
                    for (const rct3::park::Side side : { *ahead, behind })
                        for (const std::size_t k : rct3::park::path::edge_corners(side))
                            corners[k] = side == *ahead ? *up : *down;
            }
        }
        // A queue joins only across the two ends its line passes through.
        std::uint8_t open = 0b1111;
        if (path.queue)
        {
            open = 0;
            for (const rct3::park::Side side : rct3::park::kSides)
                if (rct3::park::path::edge_height(path, grid, side))
                    open |= static_cast<std::uint8_t>(1U << simulation::network_side(side));
        }
        world.paths.set_corners(id, corners, open);
        world.paths.keep_staff_off(id, kept_from_staff(world, grid, id));
        return id;
    }

    void remove_path(SimWorld& world, const save::model::PathTile& path, const save::model::TerrainGrid& grid)
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

    void follow_ground(SimWorld& world, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& was, const save::model::TerrainGrid& grid, save::model::Tile first, save::model::Tile last)
    {
        const auto inside = [&](int col, int row) { return col >= first.col && col <= last.col && row >= first.row && row <= last.row; };
        for (const save::model::PathTile& p : paths)
        {
            if (!inside(p.col, p.row) || !(rct3::park::ground::is_ground_queue(p) || (!p.elevation && p.kind == save::model::PathKind::Ground)))
                continue;
            const auto node = world.paths.find(node_spot(world.paths, p, was).first);
            if (!node)
                continue;
            const auto [spot, height] = node_spot(world.paths, p, grid);
            world.paths.set_height(*node, spot.level, height);
            (void)add_path(world, p, grid); // Lays its corners on the new ground and joins it again
        }
        // People walking or standing on those paths keep to them; guests free to, reel while the ground moves. Staff
        // have no motion for it, so they only keep to the paths.
        auto& r = world.registry;
        const auto& nodes = world.paths.nodes();
        constexpr float kFelt = 0.01f; ///< Chosen by eye: the least rise or fall under their feet they react to
        // Ground tipped past steep under their feet sends them sliding down it.
        const auto steepness = [](const save::model::TerrainGrid& g, glm::vec3 p) { return glm::length(slope([&g](glm::vec2 q) { return g.height_at(q.x, q.y).value_or(0.0f); }, { p.x, p.z })); };
        std::vector<entt::entity> sliding;
        for (const auto [e, stride, pose] : r.view<const Stride, Pose>(entt::exclude<Held, Dropped, Swimmer, RideWalk, Thrill>).each())
        {
            if (stride.from >= nodes.size() || stride.to >= nodes.size())
                continue;
            const PathTileSpot& at = nodes[stride.from].tile;
            if (!inside(at.col, at.row) && !inside(nodes[stride.to].tile.col, nodes[stride.to].tile.row))
                continue;
            if (const float now = steepness(grid, pose.position); now > kTuning.slides.steep && now > steepness(was, pose.position))
                sliding.push_back(e);
            const float rise = world.paths.surface(stride.from, stride.to, { pose.position.x, pose.position.z }) - pose.position.y;
            if (std::abs(rise) < kFelt)
                continue;
            pose.position.y += rise;
            pose.was.y += rise;
            // Already reeling, they play it through rather than start again each step of the stroke.
            if (r.all_of<Visitor>(e) && !r.any_of<Linger, Gesture>(e))
                r.emplace<Gesture>(e, GestureKind::Dizzy, kTuning.gestures.shown.value);
        }
        for (const entt::entity e : sliding)
            (void)slide(world, e); // Those it cannot move (seated, riding) stay where they are
        // Keepers about their enclosures stand on the ground itself.
        for (const auto [e, duty, pose] : r.view<const KeeperDuty, Pose>().each())
        {
            const int col = static_cast<int>(std::floor((pose.position.x - grid.origin_x) / grid.tile_size_x));
            const int row = static_cast<int>(std::floor((pose.position.z - grid.origin_z) / grid.tile_size_z));
            if (!inside(col, row))
                continue;
            if (const auto ground = grid.height_at(pose.position.x, pose.position.z))
            {
                pose.was.y += *ground - pose.position.y;
                pose.position.y = *ground;
            }
        }
    }

    std::size_t relink_attractions(SimWorld& world, const save::model::RideModel& rides)
    {
        auto& r = world.registry;
        std::size_t changed = 0;
        for (const auto [e, attraction] : r.view<Attraction>().each())
        {
            if (attraction.saved == 0)
                continue;
            if (auto* cycle = r.try_get<CycleRide>(e))
            {
                const auto ride = std::ranges::find(rides.rides, attraction.saved, [](const save::model::Ride& x) { return x.base.id; });
                if (ride == rides.rides.end() || ride->stations.empty())
                    continue;
                // A station given doors since takes the stop nearest them.
                for (std::size_t k = 1; k < ride->stations.size(); ++k)
                    if (std::ranges::none_of(cycle->stops, [&](const Stop& stop) { return stop.saved == k; }))
                        if (const auto near = nearest_platform(world.paths, ride->stations[k], cycle->stops, &Stop::platform); near && cycle->stops[*near].saved == Stop::kNone)
                            cycle->stops[*near].saved = k;
                // The way in changes only while no one queues on it; the way out at once, so riders already
                // aboard leave by a path laid to the exit while the ride is busy.
                const auto relink = [&](const save::model::Station& station, NodeId& entrance, std::vector<NodeId>& line, NodeId& exit, bool queued) {
                    const auto links = ride_links(world.paths, station);
                    if (!links)
                        return;
                    if (!queued && (links->entrance != entrance || links->line != line))
                    {
                        entrance = links->entrance;
                        line = links->line;
                        ++changed;
                    }
                    if (links->exit != exit)
                    {
                        exit = links->exit;
                        ++changed;
                    }
                };
                for (Stop& stop : cycle->stops)
                    if (stop.saved < ride->stations.size())
                        relink(ride->stations[stop.saved], stop.entrance, stop.line, stop.exit, !stop.queue.empty());
                relink(ride->stations.front(), attraction.entrance, cycle->line, cycle->exit, !cycle->queue.empty());
                continue;
            }
            // A stall, or a facility standing on its own.
            const auto stall = std::ranges::find(rides.stalls, attraction.saved, [](const save::model::Stall& x) { return x.base.id; });
            const auto facility = std::ranges::find(rides.rides, attraction.saved, [](const save::model::Ride& x) { return x.base.id; });
            NodeId entrance = kNoNode;
            if (stall != rides.stalls.end() && stall->base.spot)
                entrance = stall_front(world.paths, *stall->base.spot, stall->base.facing);
            else if (facility != rides.rides.end() && facility->kind == save::model::RideKind::Special && facility->base.spot)
                entrance = facility_door(world.paths, *facility->base.spot);
            if (entrance == kNoNode || entrance == attraction.entrance)
                continue;
            attraction.entrance = entrance;
            ++changed;
        }
        return changed;
    }

    void import_pools(SimWorld& world, const rct3::park::pools::PoolMap& pools, std::span<const rct3::park::pools::PoolExtra> scenery)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        const auto middle = [&](std::int32_t col, std::int32_t row, float height) { return glm::vec3 { paths.origin.x + (static_cast<float>(col) + 0.5f) * paths.tile_size, height * paths.level_height, paths.origin.y + (static_cast<float>(row) + 0.5f) * paths.tile_size }; };
        // Each square's complex, found by spreading from square to square side by side, at any level.
        std::multimap<std::pair<std::int32_t, std::int32_t>, const rct3::park::pools::PoolSquare*> squares;
        for (const rct3::park::pools::PoolSquare& s : pools.squares)
            squares.emplace(std::pair { s.col, s.row }, &s);
        std::map<const rct3::park::pools::PoolSquare*, std::size_t> complex_of;
        std::vector<SwimPool> complexes;
        std::vector<std::vector<rct3::park::pools::PoolSquare>> members; // By complex
        for (const rct3::park::pools::PoolSquare& first : pools.squares)
        {
            if (!complex_of.try_emplace(&first, complexes.size()).second)
                continue;
            SwimPool& complex = complexes.emplace_back();
            auto& member = members.emplace_back();
            for (std::vector<const rct3::park::pools::PoolSquare*> todo { &first }; !todo.empty();)
            {
                const rct3::park::pools::PoolSquare& s = *todo.back();
                todo.pop_back();
                member.push_back(s);
                if (s.kind == rct3::park::pools::PoolKind::Water)
                {
                    complex.water.push_back(middle(s.col, s.row, static_cast<float>(s.surface) + rct3::park::kPoolBrim));
                    auto& open = complex.open.emplace_back();
                    for (std::size_t i = 0; i < open.size(); ++i)
                    {
                        const auto mark = [](std::size_t step) { return (static_cast<float>(step) + 0.5f) / static_cast<float>(kOpenSteps) - 0.5f; };
                        open[i] = rct3::park::pools::pool_water_at(pools, s, mark(i % kOpenSteps), mark(i / kOpenSteps));
                    }
                }
                else if (s.kind == rct3::park::pools::PoolKind::Jacuzzi)
                    complex.spa.push_back(middle(s.col, s.row, static_cast<float>(s.surface) + kTuning.pools.spa_water));
                else if (s.kind == rct3::park::pools::PoolKind::Tile || s.kind == rct3::park::pools::PoolKind::Path) // Not a diving board's, a slope or stairs
                    complex.paved.push_back(middle(s.col, s.row, static_cast<float>(s.surface)) + glm::vec3 { 0, kTuning.pools.deck, 0 });
                else if (s.kind == rct3::park::pools::PoolKind::PathStairs) // At their top, where a station beside them joins them
                    complex.steps.push_back(middle(s.col, s.row, static_cast<float>(s.surface + rct3::park::pools::kPathStairsRise)) + glm::vec3 { 0, kTuning.pools.deck, 0 });
                for (const rct3::park::Side side : rct3::park::kSides)
                    for (auto [it, end] = squares.equal_range({ s.col + side.step()[0], s.row + side.step()[1] }); it != end; ++it)
                        if (complex_of.try_emplace(it->second, complexes.size() - 1).second)
                            todo.push_back(it->second);
            }
        }
        // Swimmers lie on loungers, stand under showers and stand by the other extras a while.
        for (const auto& extras : { std::span(pools.extras), scenery })
            for (const rct3::park::pools::PoolExtra& extra : extras)
                if (const auto square = pools.at(extra.col, extra.row); square && square->kind == rct3::park::pools::PoolKind::Tile && extra.fit.step > 0)
                    continue; // A ramp, a way between two squares, not a spot of its own
                else if (square && square->kind == rct3::park::pools::PoolKind::Tile)
                {
                    // Facing along the extra's length, as those on a saved park's loungers lie.
                    const auto [dc, dr] = extra.side.right().step();
                    const PoolUse use = extra.fit.lounger ? PoolUse::Lounge : extra.fit.shower ? PoolUse::Shower : PoolUse::Stand;
                    complexes[complex_of.at(&*square)].extras.push_back({ .at = middle(extra.col, extra.row, static_cast<float>(square->surface)) + glm::vec3 { 0, kTuning.pools.deck, 0 }, .heading = { std::atan2(static_cast<float>(dc), static_cast<float>(dr)) }, .use = use, .places = use == PoolUse::Lounge ? kTuning.pools.lounger_places : use == PoolUse::Shower ? kTuning.pools.shower_places : std::uint8_t { 1 } });
                }
                // Small diving boards stand in the water against its wall: swimmers step on from the paving just
                // behind it. Ladders stand at the open water's edge, a quarter tile in from the square's side, the
                // climb's motion taking those climbing out on to the coping.
                else if (square && square->kind == rct3::park::pools::PoolKind::Water && (extra.fit.board || extra.fit.ladder))
                {
                    const auto [dc, dr] = extra.side.step();
                    const glm::vec3 back { static_cast<float>(dc), 0, static_cast<float>(dr) };
                    const float deck = static_cast<float>(square->surface) + kTuning.pools.deck / paths.level_height;
                    const glm::vec3 foot = middle(extra.col, extra.row, deck) + back * (paths.tile_size / 2 + kTuning.dives.behind);
                    SwimPool& complex = complexes[complex_of.at(&*square)];
                    if (extra.fit.ladder)
                    {
                        const glm::vec3 water = middle(extra.col, extra.row, static_cast<float>(square->surface) + rct3::park::kPoolBrim) + back * (paths.tile_size / 4);
                        complex.ladders.push_back({ .water = water, .deck = glm::vec3 { water.x, deck * paths.level_height, water.z } + back * kTuning.pools.ladder_reach, .heading = { std::atan2(back.x, back.z) } });
                    }
                    else
                        complex.boards.push_back({ .foot = foot, .heading = { std::atan2(-back.x, -back.z) }, .levels = { foot.y + kTuning.dives.small_board }, .reach = kTuning.dives.behind + kTuning.dives.small_reach });
                }
        // A high diving board's ladder rises at the back of its tower, which stands at the front of its square; its
        // boards, one a level, reach out over the water it faces.
        for (const rct3::park::pools::PoolSquare& s : pools.squares)
            if (s.kind == rct3::park::pools::PoolKind::HighDive)
            {
                const auto [dc, dr] = s.side.step();
                const glm::vec3 ahead { static_cast<float>(dc), 0, static_cast<float>(dr) };
                PoolBoard board { .foot = middle(s.col, s.row, static_cast<float>(s.surface) + kTuning.pools.deck / paths.level_height) + ahead * kTuning.dives.ladder_foot + glm::vec3 { -ahead.z, 0, ahead.x } * kTuning.dives.ladder_side, .heading = { std::atan2(ahead.x, ahead.z) }, .reach = kTuning.dives.high_reach, .high = true };
                for (std::int32_t level = 0; level < std::max(s.board / rct3::park::pools::kBoardLevel, 1); ++level)
                    board.levels.push_back(static_cast<float>(s.surface + 2 + rct3::park::pools::kBoardLevel * level) * paths.level_height + kTuning.dives.board_top);
                complexes[complex_of.at(&s)].boards.push_back(std::move(board));
            }
        // The squares as swimmers go from one to the next. Two meet across a side where both are at the same height
        // there: paving, the coping round water and the water itself at the square's surface, a slope or steps at
        // its foot on the side it is entered by going up and at its top on the other. Railings stand where none
        // meets, so nobody goes off the paving, nor up or down but by a ramp or staircase.
        // shortcut: a square at a time, through the middles of the sides between; upgrade to spots within a square
        // should walkers need to go round things standing on it.
        const glm::vec3 deck { 0, kTuning.pools.deck, 0 };
        struct Meet
        {
            std::size_t complex = 0;
            std::size_t walk = 0;
            std::array<std::optional<std::int32_t>, 4> sides { }; ///< The height it meets each side of its square at
        };
        std::multimap<std::pair<std::int32_t, std::int32_t>, Meet> meets; // By tile
        for (std::size_t c = 0; c < complexes.size(); ++c)
            for (const rct3::park::pools::PoolSquare& s : members[c])
            {
                auto& walks = complexes[c].walks;
                const auto add = [&](glm::vec3 at, bool water, bool end, std::array<std::optional<std::int32_t>, 4> sides) {
                    meets.emplace(std::pair { s.col, s.row }, Meet { .complex = c, .walk = walks.size(), .sides = sides });
                    walks.push_back({ .at = at, .water = water, .end = end });
                };
                const auto level = [](std::int32_t h) { return std::array<std::optional<std::int32_t>, 4> { h, h, h, h }; };
                if (s.kind == rct3::park::pools::PoolKind::Water)
                    add(middle(s.col, s.row, static_cast<float>(s.surface) + rct3::park::kPoolBrim), true, false, level(s.surface));
                else if (rct3::park::pools::is_pool_path(s.kind) && s.kind != rct3::park::pools::PoolKind::Path)
                {
                    const std::int32_t rise = s.kind == rct3::park::pools::PoolKind::PathSlope ? rct3::park::pools::kPathSlopeRise : rct3::park::pools::kPathStairsRise;
                    std::array<std::optional<std::int32_t>, 4> sides { };
                    sides[static_cast<std::size_t>(s.side.value)] = s.surface;
                    sides[static_cast<std::size_t>(s.side.opposite().value)] = s.surface + rise;
                    add(middle(s.col, s.row, static_cast<float>(s.surface) + static_cast<float>(rise) / 2) + deck, false, false, sides);
                }
                else
                    add(middle(s.col, s.row, static_cast<float>(s.surface)) + deck, false, s.kind == rct3::park::pools::PoolKind::Jacuzzi || s.kind == rct3::park::pools::PoolKind::HighDive, level(s.surface));
                // A spiral staircase is climbed from its middle at its foot to its middle at its top.
                // shortcut: straight up its middle, not round its turns; upgrade with the steps' own path.
                if (s.kind == rct3::park::pools::PoolKind::Stairs)
                {
                    const std::int32_t top = s.surface + std::max(s.board, rct3::park::pools::kLowestStairs);
                    const glm::vec3 foot = walks.back().at;
                    add(middle(s.col, s.row, static_cast<float>(top)) + deck, false, false, level(top));
                    walks[walks.size() - 2].joins.push_back({ .to = walks.size() - 1, .via = { foot, walks.back().at } });
                    walks.back().joins.push_back({ .to = walks.size() - 2, .via = { walks.back().at, foot } });
                }
            }
        for (const auto& [tile, a] : meets)
            for (const rct3::park::Side side : rct3::park::kSides)
                for (auto [it, end] = meets.equal_range({ tile.first + side.step()[0], tile.second + side.step()[1] }); it != end; ++it)
                    if (const auto& b = it->second; a.sides[static_cast<std::size_t>(side.value)] && a.sides[static_cast<std::size_t>(side.value)] == b.sides[static_cast<std::size_t>(side.opposite().value)])
                    {
                        auto& walks = complexes[a.complex].walks;
                        const bool swim = walks[a.walk].water && walks[b.walk].water;
                        const float height = static_cast<float>(*a.sides[static_cast<std::size_t>(side.value)]);
                        const glm::vec3 meet = middle(tile.first, tile.second, swim ? height + rct3::park::kPoolBrim : height) + glm::vec3 { static_cast<float>(side.step()[0]), 0, static_cast<float>(side.step()[1]) } * (paths.tile_size / 2) + (swim ? glm::vec3 { 0.0f } : deck);
                        walks[a.walk].joins.push_back({ .to = b.walk, .via = { meet } });
                    }
        // A ramp climbs from its foot on its square to its side, where the square it meets is as much higher.
        for (const auto& extras : { std::span(pools.extras), scenery })
            for (const rct3::park::pools::PoolExtra& extra : extras)
                if (const auto square = pools.at(extra.col, extra.row); square && square->kind == rct3::park::pools::PoolKind::Tile && extra.fit.step > 0)
                {
                    const auto [dc, dr] = extra.side.step();
                    const std::int32_t top = square->surface + extra.fit.step;
                    const auto walk_at = [&](std::int32_t col, std::int32_t row, std::int32_t height) -> std::optional<std::size_t> {
                        for (auto [it, end] = meets.equal_range({ col, row }); it != end; ++it)
                            if (it->second.sides[0] == height)
                                return it->second.walk;
                        return std::nullopt;
                    };
                    const auto low = walk_at(extra.col, extra.row, square->surface);
                    const auto high = walk_at(extra.col + dc, extra.row + dr, top);
                    if (!low || !high)
                        continue;
                    const glm::vec3 ahead { static_cast<float>(dc), 0, static_cast<float>(dr) };
                    const float back = extra.fit.step > 1 ? kTuning.pools.long_ramp : kTuning.pools.short_ramp;
                    const glm::vec3 side = ahead * (paths.tile_size / 2);
                    const glm::vec3 foot = middle(extra.col, extra.row, static_cast<float>(square->surface)) + side - ahead * (back * paths.tile_size) + deck;
                    const glm::vec3 brow = middle(extra.col, extra.row, static_cast<float>(top)) + side + deck;
                    auto& walks = complexes[complex_of.at(&*square)].walks;
                    walks[*low].joins.push_back({ .to = *high, .via = { foot, brow } });
                    walks[*high].joins.push_back({ .to = *low, .via = { brow, foot } });
                }
        std::vector<rct3::park::pools::PoolExtra> all_extras(pools.extras.begin(), pools.extras.end());
        all_extras.append_range(scenery);
        for (std::size_t c = 0; c < complexes.size(); ++c)
            complexes[c].tally = rct3::park::pools::pool_tally(members[c], all_extras);
        // The rooms there were, by tile, and the complexes they opened on.
        std::map<std::pair<std::int32_t, std::int32_t>, entt::entity> rooms;
        for (const auto [e, door] : r.view<const PoolDoor>().each())
            rooms.emplace(std::pair { door.col, door.row }, e);
        std::vector<entt::entity> pool_entities(complexes.size(), entt::null);
        std::vector<entt::entity> kept;
        for (const rct3::park::pools::PoolRoom& room : pools.rooms)
        {
            const auto [dc, dr] = room.side.step();
            const auto square = pools.at(room.col + dc, room.row + dr);
            if (!square)
                continue;
            const std::size_t complex = complex_of.at(&*square);
            const auto was = rooms.find({ room.col, room.row });
            const entt::entity e = was != rooms.end() ? was->second : r.create();
            kept.push_back(e);
            entt::entity& pool = pool_entities[complex];
            if (pool == entt::null && was != rooms.end() && r.valid(r.get<PoolDoor>(e).pool) && !std::ranges::contains(pool_entities, r.get<PoolDoor>(e).pool))
                pool = r.get<PoolDoor>(e).pool;
            if (pool == entt::null)
                pool = r.create();
            const auto surface = static_cast<float>(square->surface);
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
            r.emplace_or_replace<PoolDoor>(e, PoolDoor { .pool = pool, .col = room.col, .row = room.row, .room = middle(room.col, room.row, surface), .poolside = middle(square->col, square->row, surface) + glm::vec3 { 0, kTuning.pools.deck, 0 } });
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

    std::size_t import_stalls(SimWorld& world, const save::model::RideModel& rides, const rct3::park::CarriedKinds& kinds)
    {
        std::size_t added = 0;
        for (const save::model::Stall& stall : rides.stalls)
        {
            const auto sale = sale_of(stall, kinds);
            if (!sale || !stall.base.spot)
                continue;
            const NodeId entrance = stall_front(world.paths, *stall.base.spot, stall.base.facing);
            if (entrance == kNoNode)
                continue;
            auto& r = world.registry;
            const entt::entity e = r.create();
            const bool broken = (stall.base.flags & save::model::AttractionCommon::kBrokenDown) != 0;
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = stall.base.name, .entrance = entrance, .price = sale->price, .open = stall.base.state == save::model::AttractionState::Open && !broken, .saved = stall.base.id });
            r.emplace<Upkeep>(e, Upkeep { .broken = broken, .inspect_every = { -1 }, .break_in = { 0 } });
            const PathTileSpot& tile = world.paths.nodes()[entrance].tile;
            Shop shop = sale->shop;
            shop.counter = { static_cast<float>(stall.base.spot->col - tile.col), static_cast<float>(stall.base.spot->row - tile.row) };
            r.emplace<Shop>(e, shop);
            ++added;
        }
        return added;
    }

    void restock(SimWorld& world, const save::model::Stall& stall, const rct3::park::CarriedKinds& kinds)
    {
        const auto sale = sale_of(stall, kinds);
        for (const auto [e, a] : world.registry.view<Attraction>().each())
            if (a.saved == stall.base.id && sale)
            {
                a.price = Money { sale->price };
                auto& shop = world.registry.get_or_emplace<Shop>(e);
                shop.product = sale->shop.product;
                shop.serve = sale->shop.serve;
                shop.menu = sale->shop.menu;
            }
    }

    std::optional<float> launch_of(const save::model::Ride& ride)
    {
        using enum save::model::OperatingMode;
        constexpr std::array kLaunched { PoweredLaunch, MultiplePoweredLaunch, ReverseInclinedLaunch, UpwardsLaunch, FreefallDrop };
        if (!std::ranges::contains(kLaunched, ride.mode) || ride.options.launch_speed <= 0)
            return std::nullopt;
        return ride.options.launch_speed;
    }

    std::optional<Coaster> coaster_of(const rct3::park::track::TrackCourse& course, const save::model::Ride& ride)
    {
        if (!std::ranges::contains(course.kinds, rct3::park::track::CourseKind::Station))
            return std::nullopt;
        Coaster c;
        if (!course.closed)
        {
            // A track with two ends runs back and forth: how depends on the ride's mode, how long on its circuits.
            // In a mode that goes round, the track is unfinished, and its train runs on off the end; a pool slide's
            // runs off into the pool.
            // shortcut: a drop tower's car is pushed up like a launched tower's, not hauled up; plain shuttle mode
            // and the other modes run no train; upgrade when a park has them.
            using enum save::model::OperatingMode;
            const int circuits = std::max(ride.options.circuits, 1);
            c.closed = false;
            c.splash_out = course.splash_end && ride.mode == ContinuousCircuit;
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
        c.drop_offs = course.drop_offs;
        c.wades = course.wades;
        c.points = course.points;
        c.ups = course.ups;
        for (const glm::vec3& p : course.points)
            c.height.push_back(p.y);
        for (const float d : course.distance)
        {
            const auto piece = std::ranges::upper_bound(course.piece_starts, d) - course.piece_starts.begin() - 1;
            c.covered.push_back(piece >= 0 && static_cast<std::size_t>(piece) < course.covered.size() && course.covered[static_cast<std::size_t>(piece)]);
            c.steady.push_back(piece >= 0 && static_cast<std::size_t>(piece) < course.steady.size() && course.steady[static_cast<std::size_t>(piece)]);
        }
        for (const rct3::park::track::CourseKind k : course.kinds)
        {
            using enum rct3::park::track::CourseKind;
            c.kinds.push_back(k == Chain || k == Cable ? Stretch::Chain : k == Station ? Stretch::Station : k == BlockBrake ? Stretch::BlockBrake : k == Launch ? Stretch::Launch : k == Brake ? Stretch::Brake : k == Booster ? Stretch::Booster : Stretch::Plain);
        }
        // A brake or block brake with no speed of its own, under 1 m/s, slows to the ride's for its kind.
        // shortcut: a booster with no speed of its own pushes up to the lift speed, upgrade when the speed a newly
        // built booster gets is known.
        for (std::size_t i = 0; i < course.brake_speeds.size(); ++i)
        {
            const float speed = course.brake_speeds[i];
            const Stretch kind = i < c.kinds.size() ? c.kinds[i] : Stretch::Plain;
            const float ride_speed = kind == Stretch::BlockBrake ? ride.options.block_brake_off_speed : kind == Stretch::Booster ? ride.options.lift_speed : ride.options.brake_speed;
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

    Trains trains_of(const Coaster& coaster, const save::model::Ride& ride)
    {
        Trains out;
        for (const save::model::Train& saved : ride.trains)
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
            // One kept at or past a pool slide's open end is out of sight, as if gone off it, till the station is clear.
            if (coaster.splash_out && length > 0 && saved.distance >= length)
            {
                train.at = length;
                train.running = false;
                train.splashed = true;
                out.trains.push_back(train);
                continue;
            }
            train.at = length > 0 ? std::fmod(std::fmod(saved.distance, length) + length, length) : 0;
            train.travelled = length > 0 ? std::fmod(train.at - train.start + length, length) : 0;
            constexpr float kAtStart = 0.5f; // Metres off the start that still count as standing at it
            constexpr float kStill = 0.1f;   // Metres a second that still count as standing
            if (train.travelled < kAtStart || train.travelled > length - kAtStart)
            {
                train.at = train.start;
                train.travelled = 0;
            }
            train.running = train.travelled > 0;
            train.next_stop = static_cast<std::size_t>(std::ranges::count_if(train.stops, [&](float stop) { return stop < train.travelled; }));
            // One still at another of its stations stands there, as one that has stopped at it does.
            if (const auto stop = std::ranges::find_if(train.stops, [&](float s) { return std::abs(s - train.travelled) < kAtStart; }); stop != train.stops.end() && std::abs(saved.speed) < kStill)
            {
                train.travelled = *stop;
                train.at = std::fmod(train.start + *stop, length);
                train.next_stop = static_cast<std::size_t>(stop - train.stops.begin()) + 1;
                train.running = false;
            }
            train.launched = train.travelled >= train.train_length;
            train.speed = train.running ? saved.speed : 0;
            out.trains.push_back(train);
        }
        if (out.trains.empty())
            out.trains.push_back(coaster);
        for (Coaster& train : out.trains)
            train.was_at = train.at;
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

    std::size_t import_facilities(SimWorld& world, const save::model::RideModel& rides, const std::function<std::optional<std::uint32_t>(const save::model::Ride&)>& kind_of)
    {
        std::size_t added = 0;
        for (const save::model::Ride& ride : rides.rides)
        {
            if (ride.kind != save::model::RideKind::Special || !ride.base.spot || ride.base.demolished)
                continue;
            const auto kind = kind_of(ride);
            const auto product = kind ? facility_of(*kind) : std::nullopt;
            const NodeId entrance = product ? facility_door(world.paths, *ride.base.spot) : kNoNode;
            if (entrance == kNoNode)
                continue;
            auto& r = world.registry;
            const entt::entity e = r.create();
            // Cash machines break down as stalls do; toilets and first aid do not.
            const bool breaks = *product == Product::CashMachine;
            const bool broken = breaks && (ride.base.flags & save::model::AttractionCommon::kBrokenDown) != 0;
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Shop, .name = ride.base.name, .entrance = entrance, .price = std::max<std::int64_t>(ride.ticket_price, 0), .open = ride.base.state == save::model::AttractionState::Open && !broken, .saved = ride.base.id });
            r.emplace<Shop>(e, *product);
            if (breaks)
                r.emplace<Upkeep>(e, Upkeep { .broken = broken, .inspect_every = { -1 }, .break_in = { 0 } });
            ++added;
        }
        return added;
    }

    std::optional<PathItemKind> sight_of(std::uint32_t category, rct3::park::PathFeature feature)
    {
        using enum rct3::park::PathFeature;
        if (category == std::to_underlying(rct3::park::ItemCategory::Animatronic) || category == std::to_underlying(rct3::park::ItemCategory::Effect))
            return PathItemKind::Sight;
        if (category != std::to_underlying(rct3::park::ItemCategory::PathFeature))
            return std::nullopt;
        switch (feature)
        {
            case Vista:
                return PathItemKind::PhotoSpot;
            case Speaker:
                return PathItemKind::Speaker;
            case PassportPost:
                return PathItemKind::PassportPost;
            case Fountain:
                return PathItemKind::Fountain;
            case WaterCannon:
                return PathItemKind::WaterCannon;
            case None:
                break;
        }
        return std::nullopt;
    }

    float scenery_weight(std::uint32_t category, rct3::park::PathFeature feature)
    {
        using enum rct3::park::ItemCategory;
        const auto is = [&](rct3::park::ItemCategory c) { return category == std::to_underlying(c); };
        if (is(Tree) || is(Shrub) || is(Flowers))
            return kTuning.surroundings.plant;
        if (is(SmallScenery) || is(MediumScenery))
            return kTuning.surroundings.ornament;
        if (is(Animatronic) || is(Effect) || (is(PathFeature) && feature == rct3::park::PathFeature::Fountain))
            return kTuning.surroundings.feature;
        return 0;
    }

    void import_scenery(SimWorld& world, std::span<const save::model::PlacedObject> objects, const std::function<float(const save::model::PlacedObject&)>& weight_of)
    {
        std::map<std::pair<std::int32_t, std::int32_t>, float> by_tile;
        for (const save::model::PlacedObject& o : objects)
            if (const float weight = o.hidden ? 0 : weight_of(o); weight > 0)
                by_tile[{ o.col, o.row }] += weight;
        const auto& nodes = world.paths.nodes();
        SceneryNear near { .by_node = std::vector<float>(nodes.size(), 0.0f) };
        const std::int32_t reach = kTuning.surroundings.reach;
        for (std::size_t n = 0; n < nodes.size(); ++n)
            for (std::int32_t dc = -reach; dc <= reach; ++dc)
                for (std::int32_t dr = -reach; dr <= reach; ++dr)
                    if (const auto found = by_tile.find({ nodes[n].tile.col + dc, nodes[n].tile.row + dr }); found != by_tile.end())
                        near.by_node[n] += found->second;
        world.registry.ctx().insert_or_assign(std::move(near));
    }

    void add_scenery(SimWorld& world, const save::model::PlacedObject& object, float weight)
    {
        if (weight <= 0 || object.hidden)
            return;
        const auto& nodes = world.paths.nodes();
        auto& near = world.registry.ctx().emplace<SceneryNear>().by_node;
        near.resize(std::max(near.size(), nodes.size()), 0.0f);
        for (std::size_t n = 0; n < nodes.size(); ++n)
            if (std::abs(nodes[n].tile.col - object.col) <= kTuning.surroundings.reach && std::abs(nodes[n].tile.row - object.row) <= kTuning.surroundings.reach)
                near[n] += weight;
    }

    std::size_t import_sights(SimWorld& world, std::span<const save::model::PlacedObject> objects, const std::function<std::optional<PathItemKind>(const save::model::PlacedObject&)>& sight_of)
    {
        std::size_t added = 0;
        for (const save::model::PlacedObject& o : objects)
        {
            const auto kind = o.hidden ? std::nullopt : sight_of(o);
            if (!kind)
                continue;
            if (const auto at = path_by(world.paths, o))
            {
                const glm::vec2 x = rct3::park::object::turned_x(rct3::park::ModelTurn::from(o.rotation));
                world.registry.emplace<PathItem>(world.registry.create(), PathItem { .node = *at, .kind = *kind, .object = o.id, .heading = std::atan2(x.x, x.y) });
                ++added;
            }
        }
        return added;
    }

    entt::entity import_gate(SimWorld& world, std::span<const save::model::PlacedObject> objects, const std::function<std::optional<std::array<std::int32_t, 2>>(const save::model::PlacedObject&)>& squares_of)
    {
        const PathNetwork& paths = world.paths;
        entt::entity gate = entt::null;
        EntrancePaths under;
        for (const save::model::PlacedObject& o : objects)
        {
            // A spawn point is an asset of its own, placed on the road outside the park and kept out of sight.
            if (o.symbol == "InjectionPoint")
                if (const auto at = path_by(paths, o))
                    world.registry.emplace<GuestSpawn>(world.registry.create(), *at);
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

    void keep_staff_in(SimWorld& world, const save::model::TerrainGrid& grid)
    {
        for (NodeId n = 0; n < world.paths.nodes().size(); ++n)
            world.paths.keep_staff_off(n, kept_from_staff(world, grid, n));
    }

    rct3::park::Side no_entry_side(const save::model::PlacedObject& sign)
    {
        return rct3::park::object::faced_side(rct3::park::ModelTurn::from(sign.rotation));
    }

    void import_party_mix(SimWorld& world, const save::model::GroupMix& mix)
    {
        // The save lists the kinds in PartyKind's order.
        world.registry.ctx().insert_or_assign(PartyMix { .weights = mix.weights, .single_parent = mix.single_parent });
    }

    Upkeep upkeep_of(const save::model::Ride& ride)
    {
        constexpr auto& kIntervals = kInspectionSeconds;
        const std::int32_t interval = ride.breakdown.inspection_interval;
        // Broken by its flag alone: saves keep a breakdown kind on most open, running rides too, and a repair
        // status on some fixed ones.
        const bool broken = (ride.base.flags & save::model::AttractionCommon::kBrokenDown) != 0;
        return {
            .reliability = { std::clamp(ride.breakdown.reliability / 100, 0.0f, 1.0f) },
            .broken = broken,
            .since_inspection = { std::max(ride.base.time_since_inspection, 0.0f) },
            .inspect_every = interval >= 0 && std::cmp_less(interval, kIntervals.size()) ? Seconds { kIntervals[static_cast<std::size_t>(interval)] } : Upkeep { }.inspect_every,
            .called = ride.base.mechanic_asked,
            .kind = broken ? static_cast<Breakdown>(std::to_underlying(ride.breakdown.current)) : Breakdown::None,
            .last = static_cast<Breakdown>(std::to_underlying(ride.breakdown.last)),
            .downtime = ride.breakdown.downtime_windows,
        };
    }

    entt::entity import_climb_slide(SimWorld& world, const save::model::Ride& ride, const rct3::park::track::TrackCourse& course)
    {
        if (ride.kind != save::model::RideKind::Tracked || course.climbs.empty())
            return entt::null;
        ClimbSlide slide;
        for (const auto& points : course.climbs)
        {
            if (points.size() < 2)
                continue;
            ClimbSlide::Way& way = slide.ways.emplace_back();
            Coaster& c = way.course;
            c.closed = false;
            c.points = points;
            for (const glm::vec3& p : points)
            {
                c.distance.push_back(c.distance.empty() ? 0 : c.distance.back() + glm::distance(c.points[c.distance.size() - 1], p));
                c.height.push_back(p.y);
                c.kinds.push_back(Stretch::Plain);
            }
            // The slide begins at the top of the climb.
            way.top = c.distance[static_cast<std::size_t>(std::ranges::max_element(points, { }, &glm::vec3::y) - points.begin())];
        }
        if (slide.ways.empty())
            return entt::null;
        auto& r = world.registry;
        const entt::entity e = r.create();
        r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = kNoNode, .open = ride.base.state == save::model::AttractionState::Open, .testing = ride.base.state == save::model::AttractionState::Testing, .saved = ride.base.id });
        const auto& rated = kTuning.climbs.rated;
        const bool valid = ride.assessment.valid;
        CycleRide cycle { .capacity = 0, .splash = true };
        cycle.excitement.value = rating(valid ? ride.assessment.excitement : rated[0]);
        cycle.intensity.value = rating(valid ? ride.assessment.intensity : rated[1]);
        cycle.nausea.value = rating(valid ? ride.assessment.nausea : rated[2]);
        r.emplace<CycleRide>(e, std::move(cycle));
        r.emplace<ClimbSlide>(e, std::move(slide));
        r.emplace<Upkeep>(e, upkeep_of(ride));
        if (r.get<Upkeep>(e).broken)
            r.get<Attraction>(e).open = false;
        return e;
    }

    std::vector<entt::entity> import_rides(SimWorld& world, const save::model::RideModel& rides, std::span<const std::optional<RideTiming>> flat_timings, std::span<const std::optional<Coaster>> coasters, std::span<const std::optional<std::uint32_t>> guests)
    {
        std::vector<entt::entity> added(rides.rides.size(), entt::null);
        for (std::size_t i = 0; i < rides.rides.size(); ++i)
        {
            const save::model::Ride& ride = rides.rides[i];
            std::optional<RideTiming> timing;
            const Coaster* coaster = i < coasters.size() && coasters[i] ? &*coasters[i] : nullptr;
            if (ride.kind == save::model::RideKind::Flat && i < flat_timings.size())
                timing = flat_timings[i];
            else if (ride.kind == save::model::RideKind::Tracked && !ride.seats.empty() && (coaster || (ride.stats.last_lap && ride.stats.last_lap->ride_time > 0)))
                // Each train carries its share of the ride's seats.
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size() / std::max<std::size_t>(coaster ? ride.trains.size() : 1, 1)), .run = ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0 };
            else if (ride.kind == save::model::RideKind::Tracked && !ride.seats.empty() && (ride.mode == save::model::OperatingMode::Boat || ride.mode == save::model::OperatingMode::WaterTransport))
                // shortcut: boats that sail off their dock go out all at once and come back after a set trip, with
                // no boat moving on the water; upgrade with boats steering over the lake.
                timing = RideTiming { .seats = static_cast<std::uint32_t>(ride.seats.size()), .run = kTuning.boats.trip.value };
            // A lazy river runs no trains: swimmers float round it, free on their complex's ticket, getting on and off
            // at its stations. Rated as the park has it, else as a saved park rates its river.
            if (ride.kind == save::model::RideKind::Tracked && ride.mode == save::model::OperatingMode::LazyRiver && coaster != nullptr && coaster->closed && coaster->points.size() == coaster->kinds.size())
            {
                auto& r = world.registry;
                const entt::entity e = r.create();
                r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = kNoNode, .open = ride.base.state == save::model::AttractionState::Open, .testing = ride.base.state == save::model::AttractionState::Testing, .saved = ride.base.id });
                const auto& rated = kTuning.rivers.rated;
                const auto valid = ride.assessment.valid;
                CycleRide cycle { .capacity = 0, .splash = true };
                cycle.excitement.value = rating(valid ? ride.assessment.excitement : rated[0]);
                cycle.intensity.value = rating(valid ? ride.assessment.intensity : rated[1]);
                cycle.nausea.value = rating(valid ? ride.assessment.nausea : rated[2]);
                if (i < guests.size() && guests[i])
                    cycle.room = *guests[i];
                r.emplace<CycleRide>(e, std::move(cycle));
                River river { .course = *coaster };
                const auto& kinds = coaster->kinds;
                float from = 0;
                // shortcut: a station broken by the lap's start counts as two, upgrade if a river is built that way.
                for (std::size_t k = 0; k + 1 < kinds.size(); ++k)
                {
                    if (kinds[k] != Stretch::Station)
                        continue;
                    if (k == 0 || kinds[k - 1] != Stretch::Station)
                    {
                        river.stations.emplace_back();
                        from = coaster->distance[k];
                    }
                    if (river.stations.back().points.empty())
                        river.stations.back().points.push_back(coaster->points[k]);
                    river.stations.back().points.push_back(coaster->points[k + 1]);
                    river.stations.back().at = (from + coaster->distance[k + 1]) / 2;
                }
                r.emplace<River>(e, std::move(river));
                r.emplace<Upkeep>(e, upkeep_of(ride));
                if (r.get<Upkeep>(e).broken)
                    r.get<Attraction>(e).open = false;
                added[i] = e;
                continue;
            }
            // Rides left out still make room in the park while open and working.
            const auto elsewhere = [&] {
                if (i < guests.size() && guests[i] && ride.base.state == save::model::AttractionState::Open && (ride.base.flags & save::model::AttractionCommon::kBrokenDown) == 0)
                    world.registry.ctx().emplace<RoomElsewhere>().guests += *guests[i];
            };
            // Tested or open, a tracked ride's trains run its track without doors or paths, round a whole circuit, a
            // shuttle's track, or an unfinished one to its end, as a test run needs neither; guests come once its doors
            // are on the paths (relink_attractions). Boats that sail off their dock test the same way.
            const bool boats = ride.mode == save::model::OperatingMode::Boat || ride.mode == save::model::OperatingMode::WaterTransport;
            const bool runs_alone = (coaster != nullptr || boats) && ride.base.state != save::model::AttractionState::Closed;
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
            r.emplace<Attraction>(e, Attraction { .kind = AttractionKind::Ride, .name = ride.base.name, .entrance = entrance, .price = ride.ticket_price, .open = ride.base.state == save::model::AttractionState::Open, .testing = ride.base.state == save::model::AttractionState::Testing, .saved = ride.base.id });
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
            // the others in turn, doors or not, as the game's do (opening wants doors at every station; a test run
            // stops at each all the same). Each station is the run of station track nearest its entrance.
            // shortcut: an open ride whose kind needs no doors at every station stops empty at one without them,
            // where the game's runs past; upgrade if such a tracked ride turns up.
            if (stopping && stopping->shuttle == Shuttle::None && stopping->closed && stopping->points.size() == stopping->kinds.size())
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
                std::erase_if(runs, [](const Run& run) { return run.end < 0; });
                const auto first = runs.size() > 1 && !ride.stations.empty() ? nearest_platform(world.paths, ride.stations.front(), runs, &Run::points) : std::nullopt;
                if (first)
                {
                    const float start = runs[*first].end;
                    const float length = stopping->length();
                    std::vector<std::pair<float, Stop>> others;
                    for (std::size_t k = 0; k < runs.size(); ++k)
                        if (k != *first)
                            others.push_back({ std::fmod(runs[k].end - start + length, length), Stop { .platform = runs[k].points } });
                    std::ranges::sort(others, { }, &std::pair<float, Stop>::first);
                    // A station no path reaches yet is stopped at empty till one does (relink_attractions).
                    for (std::size_t k = 1; k < ride.stations.size(); ++k)
                        if (const auto run = nearest_platform(world.paths, ride.stations[k], others, [](const auto& o) -> const auto& { return o.second.platform; }))
                            if (Stop& stop = others[*run].second; stop.saved == Stop::kNone)
                            {
                                stop.saved = k;
                                if (auto station = ride_links(world.paths, ride.stations[k]))
                                {
                                    stop.entrance = station->entrance;
                                    stop.line = std::move(station->line);
                                    stop.exit = station->exit;
                                }
                            }
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
            if (ride.kind == save::model::RideKind::Tracked)
                r.emplace<Departure>(e, departure_of(ride.options));
            added[i] = e;
        }
        return added;
    }

    void import_boats(SimWorld& world, const save::model::RideModel& rides, std::span<const entt::entity> added, std::span<const std::optional<rct3::park::track::TrackCourse>> courses, const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> water, const rct3::park::path::SolidTiles& solids)
    {
        auto& r = world.registry;
        bool any = false;
        for (std::size_t i = 0; i < std::min({ rides.rides.size(), added.size(), courses.size() }); ++i)
        {
            const save::model::Ride& ride = rides.rides[i];
            const auto& course = courses[i];
            if ((ride.mode != save::model::OperatingMode::Boat && ride.mode != save::model::OperatingMode::WaterTransport) || !r.valid(added[i]) || r.all_of<Trains>(added[i]) || !course || course->points.empty() || ride.trains.empty())
                continue;
            any = true;
            const glm::vec3 leave = course->points.back();
            const glm::vec3 heading = rct3::park::track::course_direction(*course, course->length());
            Boats boats { .dock = { .leave = { leave.x, leave.z }, .heading = { heading.x, heading.z }, .enter = { course->points.front().x, course->points.front().z } } };
            boats.roams = ride.mode == save::model::OperatingMode::Boat;
            if (ride.options.fixed_speed > 0)
                boats.top = ride.options.fixed_speed;
            boats.bumps = contains(ride.base.symbol, "bumperboat");
            for (const glm::vec3& stop : course->other_stations)
                boats.calls.emplace_back(stop.x, stop.z);
            boats.boats.resize(ride.trains.size());
            for (Boat& boat : boats.boats)
                boat.at = boats.dock.enter;
            // A ride given boats again keeps how its boats handle.
            if (const auto* had = r.try_get<Boats>(added[i]))
                boats.handling = had->handling;
            r.emplace_or_replace<Boats>(added[i], std::move(boats));
        }
        if (any)
            boat_water(world, grid, water, solids);
    }

    void boat_water(SimWorld& world, const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> water, const rct3::park::path::SolidTiles& solids)
    {
        auto& r = world.registry;
        if (r.view<const Boats>().empty())
            return;
        // Each dock's own water stays open, whatever stands over it.
        WaterTiles tiles = water_tiles(grid, water, solids);
        const WaterTiles open = water_tiles(grid, water);
        for (const auto [_, boats] : r.view<const Boats>().each())
        {
            const glm::ivec2 dock = tiles.tile_of(boats.dock.enter);
            if (dock.x >= 0 && dock.y >= 0 && dock.x < tiles.cols && dock.y < tiles.rows)
                tiles.level[static_cast<std::size_t>(dock.y) * static_cast<std::size_t>(tiles.cols) + static_cast<std::size_t>(dock.x)] = open.at(dock.x, dock.y);
        }
        r.ctx().insert_or_assign(std::move(tiles));
    }

    void link_stations(SimWorld& world, std::span<const entt::entity> added, std::span<const std::optional<rct3::park::track::TrackCourse>> courses, const save::model::TerrainGrid& grid)
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
                if (course->kinds[k] != rct3::park::track::CourseKind::Station)
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

    std::size_t import_litter(SimWorld& world, const save::model::PeopleModel& people, float now)
    {
        std::size_t added = 0;
        for (const save::model::Litter& l : people.litter)
        {
            const auto at = l.state == save::model::LitterState::OnGround ? world.paths.nearest({ l.position.x, l.position.y, l.position.z }) : std::nullopt;
            if (!at)
                continue;
            const float age = std::max(now - l.create_time, 0.0f) * kSecondsPerHour / park::kCalendarUnitsPerHour;
            world.registry.emplace<Litter>(world.registry.create(), Litter { *at, l.type, age });
            ++added;
        }
        return added;
    }

    std::size_t import_path_items(SimWorld& world, std::span<const save::model::PlacedObject> placed, std::span<const save::model::BinState> bins, std::span<const std::uint64_t> benches, std::span<const save::model::SignState> signs)
    {
        std::unordered_map<std::uint64_t, const save::model::PlacedObject*> objects;
        for (const save::model::PlacedObject& o : placed)
            objects.emplace(o.id, &o);
        auto& r = world.registry;
        std::size_t added = 0;
        const auto add = [&](std::uint64_t object, PathItemKind kind) -> entt::entity {
            const auto found = objects.find(object);
            const entt::entity e = found == objects.end() ? entt::null : add_path_item(world, *found->second, kind);
            added += e != entt::null ? 1 : 0;
            return e;
        };
        for (const save::model::BinState& b : bins)
            if (const entt::entity e = add(b.object_ref, PathItemKind::Bin); e != entt::null)
                r.replace<Bin>(e, Bin { b.capacity / kSavedBinPiece, std::round(b.contents / kSavedBinPiece) });
        for (const std::uint64_t bench : benches)
            add(bench, PathItemKind::Bench);
        for (const save::model::SignState& s : signs)
            if (const entt::entity e = add(s.object_ref, PathItemKind::Sign); e != entt::null && s.kind == 1)
                world.paths.set_no_entry(r.get<PathItem>(e).node, no_entry_side(*objects.at(s.object_ref)), true);
        return added;
    }

    void set_wall(SimWorld& world, const rct3::park::EdgeWall& wall, bool standing)
    {
        if (standing)
            world.paths.add_wall(wall.tile.col, wall.tile.row, wall.side, wall.low, wall.high);
        else
            world.paths.remove_wall(wall.tile.col, wall.tile.row, wall.side, wall.low, wall.high);
    }

    entt::entity add_path_item(SimWorld& world, const save::model::PlacedObject& o, PathItemKind kind)
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
        const glm::vec2 x = rct3::park::object::turned_x(rct3::park::ModelTurn::from(o.rotation));
        r.emplace<PathItem>(e, PathItem { .node = *at, .kind = kind, .object = o.id, .heading = std::atan2(x.x, x.y) });
        if (kind == PathItemKind::Bin)
            r.emplace<Bin>(e, Bin { kNewBinPieces, 0 });
        return e;
    }

    entt::entity add_flower_bed(SimWorld& world, const save::model::PlacedObject& o, float capacity, std::optional<float> water)
    {
        const auto at = o.hidden ? std::nullopt : path_by(world.paths, o);
        if (!at)
            return entt::null;
        const entt::entity e = world.registry.create();
        world.registry.emplace<FlowerBed>(e, FlowerBed { .node = *at, .object = o.id, .capacity = capacity, .water = water && *water >= 0 ? std::min(*water, capacity) : capacity });
        return e;
    }

    std::size_t import_flower_beds(SimWorld& world, std::span<const save::model::PlacedObject> objects, std::span<const save::model::FlowerBedState> beds, const std::function<std::optional<float>(const save::model::PlacedObject&)>& capacity_of)
    {
        std::size_t added = 0;
        for (const save::model::PlacedObject& o : objects)
            if (const auto capacity = capacity_of(o))
            {
                const auto saved = std::ranges::find(beds, o.id, &save::model::FlowerBedState::object_ref);
                added += add_flower_bed(world, o, *capacity, saved != beds.end() ? std::optional(saved->water) : std::nullopt) != entt::null ? 1 : 0;
            }
        return added;
    }

    std::vector<entt::entity> import_staff(SimWorld& world, const save::model::PeopleModel& people)
    {
        auto& r = world.registry;
        std::vector<entt::entity> out(people.staff.size(), entt::null);
        for (std::size_t i = 0; i < people.staff.size(); ++i)
        {
            const save::model::Person& p = people.staff[i].person;
            const glm::vec3 position { p.position.x, p.position.y, p.position.z };
            const StaffRole role = staff_role(people.staff[i].kind);
            // A vendor stays in their booth, a keeper in their enclosure (import_animal_houses gives them their work), a
            // lifeguard in their chair, where the park left them. A lifeguard comes and goes with their chair.
            const bool walks = walks_paths(role);
            if (const auto* guard = std::get_if<save::model::LifeGuardWork>(&people.staff[i].work); guard != nullptr && guard->chair.is_none())
                continue;
            auto at = world.paths.nearest(position);
            if (!at || world.paths.nodes()[*at].queue)
                at = walkway_near(world.paths, position);
            if (!at && walks)
                continue;
            const entt::entity e = r.create();
            const save::model::Staff& saved = people.staff[i];
            Staff staff { .role = role, .tasks = saved.task_flags, .training = std::clamp(saved.training, 0, kMostTraining), .warnings = saved.warnings, .happiness = std::clamp(saved.happiness, 0.0f, 1.0f), .laziness = std::clamp(saved.laziness, 0.0f, 1.0f) };
            set_wage_level(staff, saved.wage_level);
            take_saved_work(staff, saved);
            r.emplace<Staff>(e, staff);
            r.emplace<Pose>(e, position, -p.heading, position, -p.heading); // The original turns the other way
            if (walks)
                start_walking(world, e, *at);
            // An inspector the park left swimming, looking a pool complex over: in it still, a swimmer till their
            // time there is up, then back to their rounds by its changing room.
            if (const auto door = walks && role == StaffRole::Inspector && p.state == save::model::PersonState::Swimming ? pool_door_at(world, position) : std::nullopt)
            {
                const SwimPool& pool = r.get<SwimPool>(r.get<PoolDoor>(*door).pool);
                const float half = world.paths.tile_size / 2;
                const auto within = [&](const glm::vec3& w) { return std::abs(position.x - w.x) <= half && std::abs(position.z - w.z) <= half; };
                r.emplace<Needs>(e);
                r.emplace<Mood>(e);
                r.emplace<Wallet>(e);
                r.emplace<Rounds>(e, Rounds { .seen = { { *door, 0.0f } } });
                r.get<Errand>(e) = { .kind = ErrandKind::Swimming, .target = *door };
                const Seconds left = saved.time_in_pool ? Seconds { std::max(*saved.time_in_pool, 0.0f) } : kTuning.pools.stay;
                r.emplace<Swimmer>(e, Swimmer { .changed = true, .left = left, .goal = position, .in_water = std::ranges::any_of(pool.water, within) });
                auto& pose = r.get<Pose>(e);
                pose.position = pose.was = position;
            }
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

    entt::entity add_lifeguard(SimWorld& world, glm::vec3 at, Radians heading)
    {
        auto& r = world.registry;
        const entt::entity e = r.create();
        Staff staff { .role = StaffRole::Other };
        set_wage_level(staff, staff.wage_level);
        r.emplace<Staff>(e, staff);
        r.emplace<Pose>(e, Pose { .position = at, .heading = heading, .was = at, .was_heading = heading });
        return e;
    }

    entt::entity add_staff(SimWorld& world, StaffRole role, NodeId at)
    {
        const entt::entity e = world.registry.create();
        // An inspector's mood never changes; saved ones are all half content.
        Staff staff { .role = role, .happiness = role == StaffRole::Inspector ? 0.5f : 1.0f };
        set_wage_level(staff, staff.wage_level);
        world.registry.emplace<Staff>(e, staff);
        start_walking(world, e, at);
        return e;
    }

    Departure departure_of(const save::model::OperatingOptions& options)
    {
        const bool listed = options.wait_for_load >= 0 && options.wait_for_load <= static_cast<std::int32_t>(LoadWait::Any);
        return { .wait_for = listed ? static_cast<LoadWait>(options.wait_for_load) : LoadWait::Any, .min_wait = std::max(options.min_wait, -1), .max_wait = std::max(options.max_wait, -1), .leave_on_arrival = options.leave_if_other_train_enters_station, .sync = options.sync_trains };
    }

    std::size_t board_saved_riders(SimWorld& world, const save::model::PeopleModel& people, std::span<const entt::entity> guests, const save::model::RideModel& saved, std::span<const entt::entity> rides)
    {
        auto& r = world.registry;
        std::unordered_map<save::model::ObjectId, entt::entity> ride_of;
        // Not a ride run with no doors on the paths, as its riders would have nowhere to get off, but for a pool
        // slide, whose riders swim on in the pool it ends in or lets them off into.
        const auto slide_room = [&](entt::entity ride) -> std::optional<entt::entity> {
            const auto* fleet = r.try_get<Trains>(ride);
            if (fleet == nullptr || fleet->trains.empty() || (!fleet->trains.front().splash_out && fleet->trains.front().drop_offs.empty()))
                return std::nullopt;
            const auto pool = pool_ride_pool(world, ride);
            for (const auto [door, d] : r.view<const PoolDoor>().each())
                if (pool && d.pool == *pool)
                    return door;
            return std::nullopt;
        };
        for (std::size_t i = 0; i < saved.rides.size() && i < rides.size(); ++i)
            if (rides[i] != entt::null && (r.get<Attraction>(rides[i]).entrance != kNoNode || slide_room(rides[i]) || r.all_of<River>(rides[i])))
                ride_of.emplace(saved.rides[i].base.id, rides[i]);
        std::size_t seated = 0;
        for (std::size_t i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const save::model::Person& p = people.guests[i].person;
            const auto ride = ride_of.find(p.state_target.id);
            const entt::entity e = guests[i];
            if (e == entt::null || p.state != save::model::PersonState::OnRide || ride == ride_of.end())
                continue;
            auto& cycle = r.get<CycleRide>(ride->second);
            // On a lazy river, at the nearest of its course to where the park kept them, in their ring; they swim on
            // in the complex they get off in.
            // shortcut: the room they swim on from till then is the nearest, upgrade when the save's own is read.
            if (const auto* river = r.try_get<River>(ride->second); river != nullptr && r.all_of<Pose, Dice>(e))
            {
                const glm::vec3 at { p.position.x, p.position.y, p.position.z };
                const auto& points = river->course.points;
                std::optional<entt::entity> room;
                float closest = std::numeric_limits<float>::max();
                for (const entt::entity door : r.view<const PoolDoor>())
                    if (const float d = glm::distance(r.get<PoolDoor>(door).poolside, at); d < closest)
                    {
                        closest = d;
                        room = door;
                    }
                if (points.empty() || !room)
                    continue;
                const auto near = std::ranges::min_element(points, { }, [&](const glm::vec3& q) { return glm::distance(q, at); });
                auto& dice = r.get<Dice>(e).state;
                const auto& tuning = kTuning.rivers;
                const float length = river->course.length();
                r.emplace_or_replace<Floating>(
                    e, Floating { .river = ride->second, .along = river->course.distance[static_cast<std::size_t>(near - points.begin())], .stay = static_cast<float>(random::between(dice, tuning.least_stay, tuning.most_stay)) * length, .lane = static_cast<float>(random::between(dice, -1.0, 1.0)), .spin = static_cast<float>(random::between(dice, -tuning.spin, tuning.spin)), .paddle = { static_cast<float>(random::between(dice, tuning.least_paddle.value, tuning.most_paddle.value)) } });
                r.emplace_or_replace<Swimmer>(e, Swimmer { .changed = true, .left = kTuning.pools.stay, .in_water = true, .room = *room });
                r.get<Errand>(e) = { ErrandKind::Riding, ride->second, 0 };
                cycle.riders.push_back(e);
                ++seated;
                continue;
            }
            // Off the ride they get off where its riders do; off a slide, in their swimwear, into its pool.
            const auto room = slide_room(ride->second);
            const NodeId off = cycle.exit != kNoNode ? cycle.exit : r.get<Attraction>(ride->second).entrance;
            if (!r.all_of<Stride>(e) && off == kNoNode)
                continue;
            if (!r.all_of<Stride>(e))
                start_walking(world, e, off);
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
            if (room)
                r.emplace_or_replace<Swimmer>(e, Swimmer { .changed = true, .left = kTuning.pools.stay, .room = *room });
            ++seated;
        }
        return seated;
    }

    std::size_t resume_saved_swimmers(SimWorld& world, const save::model::PeopleModel& people, std::span<const entt::entity> guests)
    {
        auto& r = world.registry;
        const float half = world.paths.tile_size / 2;
        std::size_t resumed = 0;
        for (std::size_t i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const save::model::Guest& saved = people.guests[i];
            const save::model::PersonState state = saved.person.state;
            const entt::entity e = guests[i];
            if (e == entt::null || (state != save::model::PersonState::Swimming && state != save::model::PersonState::Jacuzzi && state != save::model::PersonState::HighDive))
                continue;
            const glm::vec3 at { saved.person.position.x, saved.person.position.y, saved.person.position.z };
            const auto within = [&](const glm::vec3& w) { return std::abs(at.x - w.x) <= half && std::abs(at.z - w.z) <= half; };
            for (const auto [door, pool_door] : r.view<const PoolDoor>().each())
            {
                const auto* pool = r.valid(pool_door.pool) ? r.try_get<SwimPool>(pool_door.pool) : nullptr;
                if (pool == nullptr || (std::ranges::none_of(pool->water, within) && std::ranges::none_of(pool->paved, within) && std::ranges::none_of(pool->spa, within)))
                    continue;
                r.get<Errand>(e) = { .kind = ErrandKind::Swimming, .target = door };
                const Seconds left = saved.time_in_pool ? Seconds { std::max(*saved.time_in_pool, 0.0f) } : kTuning.pools.stay;
                r.emplace_or_replace<Swimmer>(e, Swimmer { .changed = true, .left = left, .goal = at, .in_water = std::ranges::any_of(pool->water, within) || std::ranges::any_of(pool->spa, within), .to_spa = state == save::model::PersonState::Jacuzzi });
                auto& pose = r.get<Pose>(e);
                pose.position = pose.was = at;
                ++resumed;
                break;
            }
        }
        return resumed;
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
                // Off back where they got on: not every station of the ride need have a way out.
                r.get<Errand>(e) = { .kind = ErrandKind::Queueing, .target = on->second.first, .station = stations.at(*at), .off = stations.at(*at) };
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
