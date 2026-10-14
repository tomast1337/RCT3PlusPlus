// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_supports.hpp"

#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"
#include "park/ground_queues.hpp"
#include "park/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace rct3::park
{

    void add_support_piece(SupportSets& sets, std::string_view group, std::uint32_t type, std::string_view symbol, std::int32_t levels)
    {
        if (group.empty() || symbol.empty() || levels <= 0)
            return;
        if (type != kSupportMiddleType && type != kSupportFootType && type != kSupportDeepFootType)
            return;
        auto set = sets.find(group);
        if (set == sets.end())
            set = sets.emplace(std::string(group), SupportSet { }).first;
        SupportPiece piece { std::string(symbol), levels };
        if (type == kSupportFootType)
        {
            if (!set->second.foot)
                set->second.foot = std::move(piece);
            return;
        }
        if (type == kSupportDeepFootType)
        {
            if (!set->second.deep_foot)
                set->second.deep_foot = std::move(piece);
            return;
        }
        auto& middles = set->second.middles;
        if (std::ranges::any_of(middles, [&](const SupportPiece& m) { return m.levels == levels; }))
            return;
        middles.insert(std::ranges::find_if(middles, [&](const SupportPiece& m) { return m.levels < levels; }), std::move(piece));
    }

    std::expected<SupportSets, std::string> load_support_sets(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        SupportSets sets;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::SceneryItemEntry& entry : ovl::read_scenery_items(*overlay, *part))
            {
                if (!entry.item || entry.item->squares.empty())
                    continue;
                const auto& squares = entry.item->squares;
                const std::int32_t levels = std::ranges::max(squares, { }, &ovl::SceneryItemSquare::max_height).max_height - std::ranges::min(squares, { }, &ovl::SceneryItemSquare::min_height).min_height;
                add_support_piece(sets, entry.item->group, entry.item->type, entry.name, levels);
            }
        }
        return sets;
    }

    std::vector<StackedPiece> support_stack(const SupportSet& set, float ground_low, float ground_high, std::int32_t top)
    {
        std::vector<StackedPiece> out;
        auto level = static_cast<std::int32_t>(std::ceil(ground_high - kFlat));
        const bool uneven = static_cast<float>(level) - ground_low > kFlat;
        const std::optional<SupportPiece>& foot = uneven && set.deep_foot ? set.deep_foot : set.foot;
        if (!foot || level + foot->levels > top)
            return out;
        out.push_back({ foot->symbol, level });
        level += foot->levels;
        for (const SupportPiece& middle : set.middles)
        {
            while (level + middle.levels <= top)
            {
                out.push_back({ middle.symbol, level });
                level += middle.levels;
            }
        }
        return out;
    }

    std::vector<save::PlacedObject> path_supports(std::span<const save::PathTile> paths, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const SupportSets& sets)
    {
        std::uint64_t last_id = 0;
        std::unordered_map<std::uint64_t, const save::PlacedObject*> by_id;
        for (const save::PlacedObject& o : objects)
        {
            last_id = std::max(last_id, o.id);
            by_id.emplace(o.id, &o);
        }

        // Tiles whose ground has a path: nothing stands there.
        std::set<std::pair<int, int>> on_ground;
        for (const save::PathTile& p : paths)
        {
            if (!p.elevation || is_ground_queue(p))
                on_ground.emplace(p.col, p.row);
        }

        // The lowest raised piece on each tile, with what holds it up.
        struct Held
        {
            const save::PlacedObject* piece = nullptr;
            const SupportSet* set = nullptr;
            std::int32_t top = 0;
        };
        std::map<std::pair<int, int>, Held> lowest;
        for (const save::PathTile& p : paths)
        {
            if (!p.elevation || is_ground_queue(p) || p.elevation->underground || on_ground.contains({ p.col, p.row }) || !grid.contains(p.col, p.row))
                continue;
            const auto piece = by_id.find(p.elevation->scenery_ref);
            if (piece == by_id.end() || !piece->second->base_height)
                continue;
            const auto model = models.find(model_key(*piece->second));
            if (model == models.end())
                continue;
            const auto set = sets.find(model->second.support_group);
            if (set == sets.end())
                continue;
            const std::int32_t top = *piece->second->base_height + model->second.lowest_level;
            auto [at, added] = lowest.try_emplace({ p.col, p.row }, Held { piece->second, &set->second, top });
            if (!added && top < at->second.top)
                at->second = { piece->second, &set->second, top };
        }

        std::vector<save::PlacedObject> out;
        for (const auto& [tile, held] : lowest)
        {
            const save::TerrainCell& cell = grid.at(tile.first, tile.second);
            for (const StackedPiece& s : support_stack(*held.set, cell.min_height(), cell.max_height(), held.top))
            {
                save::PlacedObject o;
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.symbol = s.symbol;
                o.overlay = std::string(kSupportOverlay);
                o.col = tile.first;
                o.row = tile.second;
                o.base_height = s.level;
                o.height_adjust = 0.0f;
                o.force_absolute_height = true;
                o.rotation = held.piece->rotation;
                o.colours = held.piece->colours;
                out.push_back(std::move(o));
            }
        }
        return out;
    }

} // namespace rct3::park
