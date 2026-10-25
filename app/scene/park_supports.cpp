// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_supports.hpp"

#include "logging/log.hpp"
#include "park/support_columns.hpp"
#include "scene/park_scene.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace rct3::scene
{

    namespace
    {

        SupportWatch::Seen seen(const ParkScene& scene, const save::PlacedObject& o)
        {
            return { o.symbol, o.overlay, o.col, o.row, o.rotation, o.base_height, o.hidden, park::support_tiles(o, scene.models) };
        }

        bool same(const SupportWatch::Seen& s, const save::PlacedObject& o)
        {
            return s.symbol == o.symbol && s.overlay == o.overlay && s.col == o.col && s.row == o.row && s.rotation == o.rotation && s.base_height == o.base_height && s.hidden == o.hidden;
        }

        std::vector<std::pair<int, int>> ground_paths(const ParkScene& scene)
        {
            std::vector<std::pair<int, int>> out;
            for (const save::PathTile& p : scene.footpaths.paths)
            {
                if (park::path_at_ground(p, scene.grid))
                    out.emplace_back(p.col, p.row);
            }
            std::ranges::sort(out);
            out.erase(std::ranges::unique(out).begin(), out.end());
            return out;
        }

        std::vector<std::array<float, 4>> ground(const save::TerrainGrid& grid)
        {
            std::vector<std::array<float, 4>> out;
            out.reserve(grid.cells.size());
            for (const save::TerrainCell& c : grid.cells)
                out.push_back(c.corners);
            return out;
        }

    } // namespace

    void watch_supports(ParkScene& scene)
    {
        SupportWatch& w = scene.supports;
        w.objects.clear();
        for (const save::PlacedObject& o : scene.objects)
        {
            if (o.overlay != park::kSupportOverlay)
                w.objects.insert_or_assign(o.id, seen(scene, o));
        }
        w.ground_paths = ground_paths(scene);
        w.ground = ground(scene.grid);
        w.stale = false;
    }

    void refresh_supports(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        SupportWatch& w = scene.supports;
        if (!w.stale)
            return;
        w.stale = false;
        auto started = std::chrono::steady_clock::now();

        // The tiles where anything that holds up columns or is in their way changed.
        std::set<park::SupportTile> tiles;
        std::unordered_map<std::uint64_t, SupportWatch::Seen> now;
        for (const save::PlacedObject& o : scene.objects)
        {
            if (o.overlay == park::kSupportOverlay)
                continue;
            const auto before = w.objects.find(o.id);
            if (before != w.objects.end() && same(before->second, o))
            {
                now.insert(w.objects.extract(before));
                continue;
            }
            SupportWatch::Seen s = seen(scene, o);
            tiles.insert(s.tiles.begin(), s.tiles.end());
            if (before != w.objects.end())
                tiles.insert(before->second.tiles.begin(), before->second.tiles.end());
            now.insert_or_assign(o.id, std::move(s));
        }
        for (const auto& [_, gone] : w.objects)
            tiles.insert(gone.tiles.begin(), gone.tiles.end());
        w.objects = std::move(now);
        std::vector<std::pair<int, int>> paths = ground_paths(scene);
        std::ranges::set_symmetric_difference(paths, w.ground_paths, std::inserter(tiles, tiles.end()));
        w.ground_paths = std::move(paths);
        std::vector<std::array<float, 4>> corners = ground(scene.grid);
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            if (i >= w.ground.size() || corners[i] != w.ground[i])
                tiles.emplace(static_cast<int>(i % static_cast<std::size_t>(scene.grid.width)), static_cast<int>(i / static_cast<std::size_t>(scene.grid.width)));
        }
        w.ground = std::move(corners);
        const park::SupportSets* sets = support_sets(scene, assets);
        if (tiles.empty() || sets == nullptr)
            return;

        // The time waited for the objects' mesh build in hand is not the supports'.
        const auto waited = std::chrono::steady_clock::now();
        wait_for_build(scene);
        started += std::chrono::steady_clock::now() - waited;
        // The old pieces go. Their places in the objects are kept for new ones, so no other object moves.
        std::vector<std::size_t> spare;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            save::PlacedObject& o = scene.objects[i];
            if (o.overlay != park::kSupportOverlay)
                continue;
            if (const auto column = scene.columns.find(o.id); column != scene.columns.end() && tiles.contains(column->second))
            {
                scene.fitted.erase(o.id);
                scene.columns.erase(column);
                o.hidden = true;
            }
            if (o.hidden && !scene.columns.contains(o.id))
                spare.push_back(i);
        }
        std::uint64_t last = 0;
        for (const save::PlacedObject& o : scene.objects)
            last = std::max(last, o.id);
        park::BuiltSupports built = park::support_columns(scene.objects, scene.models, scene.footpaths.paths, scene.grid, *sets, last, supports_passed(scene), tiles);
        for (std::size_t i = 0; i < built.pieces.size(); ++i)
        {
            save::PlacedObject& piece = built.pieces[i];
            if (!load_object_model(scene, scene.footpaths.models, assets, piece))
                continue;
            scene.columns.emplace(piece.id, built.of[i].tile);
            if (const auto fit = built.fitted.find(piece.id); fit != built.fitted.end())
                scene.fitted.insert_or_assign(piece.id, fit->second);
            if (spare.empty())
                scene.objects.push_back(std::move(piece));
            else
            {
                scene.objects[spare.back()] = std::move(piece);
                spare.pop_back();
            }
        }
        objects_changed(scene);
        w.stale = false; // That was only the supports
        logging::logger("park").info("supports on {} tiles built again in {} us", tiles.size(), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    }

} // namespace rct3::scene
