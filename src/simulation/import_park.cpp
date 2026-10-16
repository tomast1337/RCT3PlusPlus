// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "simulation/components.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string_view>

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
    } // namespace

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
            int level = 0;
            if (p.elevation && p.elevation->slope != save::kSlopeOnGround)
                level = p.elevation->quantised_height;
            else
            {
                const float x = paths.origin.x + (static_cast<float>(p.col) + 0.5f) * paths.tile_size;
                const float z = paths.origin.y + (static_cast<float>(p.row) + 0.5f) * paths.tile_size;
                level = static_cast<int>(std::lround(grid.height_at(x, z).value_or(0.0f) / paths.level_height));
            }
            paths.add({ p.col, p.row, level }, p.kind == save::PathKind::Queue);
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
            const save::PlacementSpot& spot = *stall.base.spot;
            // The entrance is the path beside the stall closest to its height, trying the side it faces first.
            NodeId entrance = kNoNode;
            int gap = 0;
            constexpr std::array<std::array<int, 2>, 4> kSides { { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } };
            for (int k = 0; k < 4; ++k)
            {
                const auto& [dc, dr] = kSides[static_cast<std::size_t>((stall.base.facing + k) % 4)];
                for (const PathNode& n : world.paths.nodes())
                {
                    if (n.queue || n.tile.col != spot.col + dc || n.tile.row != spot.row + dr)
                        continue;
                    const int g = std::abs(n.tile.level - spot.height);
                    if (entrance == kNoNode || g < gap)
                    {
                        entrance = *world.paths.find(n.tile);
                        gap = g;
                    }
                }
            }
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

    std::size_t start_guests(SimWorld& world)
    {
        auto& r = world.registry;
        const auto view = r.view<const Pose, const Visitor>();
        std::vector<entt::entity> guests(view.begin(), view.end());
        std::ranges::sort(guests);
        std::size_t started = 0;
        for (const entt::entity e : guests)
        {
            if (const auto at = world.paths.nearest(r.get<Pose>(e).position); at && !world.paths.nodes()[*at].queue)
            {
                start_walking(world, e, *at);
                ++started;
            }
        }
        return started;
    }

} // namespace rct3::simulation
