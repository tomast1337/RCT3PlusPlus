// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/supports.hpp"

#include "logging/log.hpp"
#include "park/support/columns.hpp"
#include "profiling/profiler.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/scene.hpp"
#include "ui/interface_assets.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {

        SupportWatch::Seen seen(const ParkScene& scene, const save::model::PlacedObject& o)
        {
            return { o.symbol, o.overlay, o.col, o.row, o.rotation, o.base_height, o.hidden, o.support_set, rct3::park::support::support_tiles(o, scene.models) };
        }

        bool same(const SupportWatch::Seen& s, const save::model::PlacedObject& o)
        {
            return s.symbol == o.symbol && s.overlay == o.overlay && s.col == o.col && s.row == o.row && s.rotation == o.rotation && s.base_height == o.base_height && s.hidden == o.hidden && s.support_set == o.support_set;
        }

        std::vector<std::pair<int, int>> ground_paths(const ParkScene& scene)
        {
            std::vector<std::pair<int, int>> out;
            for (const save::model::PathTile& p : scene.footpaths.paths)
            {
                if (rct3::park::support::path_at_ground(p, scene.grid))
                    out.emplace_back(p.col, p.row);
            }
            std::ranges::sort(out);
            out.erase(std::ranges::unique(out).begin(), out.end());
            return out;
        }

        std::vector<std::array<float, 4>> ground(const save::model::TerrainGrid& grid)
        {
            std::vector<std::array<float, 4>> out;
            out.reserve(grid.cells.size());
            for (const save::model::TerrainCell& c : grid.cells)
                out.push_back(c.corners);
            return out;
        }

    } // namespace

    void watch_supports(ParkScene& scene)
    {
        SupportWatch& w = scene.supports;
        w.objects.clear();
        for (const save::model::PlacedObject& o : scene.objects)
        {
            if (o.overlay != rct3::park::support::kSupportOverlay)
                w.objects.insert_or_assign(o.id, seen(scene, o));
        }
        w.ground_paths = ground_paths(scene);
        w.ground = ground(scene.grid);
        w.stale = false;
    }

    void refresh_supports(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        SupportWatch& w = scene.supports;
        // Not while the objects' mesh build reads them: they are looked at once it is done, and no new build
        // starts while they are stale (refresh_objects), so the frame does not wait for one.
        if (!w.stale || (scene.object_build.valid() && !ready(scene.object_build)))
            return;
        w.stale = false;
        auto started = std::chrono::steady_clock::now();
        std::optional<profiling::Scope> diff_zone;
        diff_zone.emplace("Diff");

        // The tiles where anything that holds up columns or is in their way changed.
        std::set<rct3::park::support::SupportTile> tiles;
        std::unordered_map<std::uint64_t, SupportWatch::Seen> now;
        for (const save::model::PlacedObject& o : scene.objects)
        {
            if (o.overlay == rct3::park::support::kSupportOverlay)
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
        const rct3::park::support::SupportSets* sets = support_sets(scene, assets);
        if (tiles.empty() || sets == nullptr)
            return;

        diff_zone.reset();
        // The time waited for the objects' mesh build in hand is not the supports'.
        const auto waited = std::chrono::steady_clock::now();
        profiling::section("Wait", [&] { wait_for_build(scene); });
        started += std::chrono::steady_clock::now() - waited;
        // The old pieces go. Their places in the objects are kept for new ones, so no other object moves.
        std::vector<std::size_t> spare;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            save::model::PlacedObject& o = scene.objects[i];
            if (o.overlay != rct3::park::support::kSupportOverlay)
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
        for (const save::model::PlacedObject& o : scene.objects)
            last = std::max(last, o.id);
        place_objects(scene); // Their models, kept
        rct3::park::support::BuiltSupports built = profiling::section("Columns", [&] { return rct3::park::support::support_columns(scene.objects, scene.models, scene.footpaths.paths, scene.grid, *sets, last, supports_passed(scene), tiles, scene.places.models); });
        for (std::size_t i = 0; i < built.pieces.size(); ++i)
        {
            save::model::PlacedObject& piece = built.pieces[i];
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
        logging::logger("park").debug("supports on {} tiles built again in {} us", tiles.size(), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    }

    const rct3::park::support::SupportSets* support_sets(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.support_sets && assets.install)
        {
            if (const auto found = assets.install->find(std::format("{}.common.ovl", rct3::park::support::kSupportOverlay)))
            {
                if (auto sets = rct3::park::support::load_support_sets(*found))
                    f.support_sets = std::move(*sets);
                else
                    logging::logger("park").warning("supports: {}", sets.error());
            }
        }
        return f.support_sets ? &*f.support_sets : nullptr;
    }

    std::vector<save::model::PlacedObject> supports_under(const ParkScene& scene, std::span<const save::model::PlacedObject> ghosts, const rct3::park::support::SupportSets& sets)
    {
        std::vector<save::model::PlacedObject> objects = scene.objects;
        std::uint64_t last = 0;
        for (const save::model::PlacedObject& o : objects)
            last = std::max(last, o.id);
        std::set<rct3::park::support::SupportTile> tiles;
        std::unordered_set<std::uint64_t> ids;
        for (const save::model::PlacedObject& ghost : ghosts)
        {
            save::model::PlacedObject& o = objects.emplace_back(ghost);
            o.id = ++last;
            const auto under = rct3::park::support::support_tiles(o, scene.models);
            tiles.insert(under.begin(), under.end());
            ids.insert(o.id);
        }
        rct3::park::support::BuiltSupports built = rct3::park::support::support_columns(objects, scene.models, scene.footpaths.paths, scene.grid, sets, last, supports_passed(scene), tiles);
        std::vector<save::model::PlacedObject> out;
        for (std::size_t i = 0; i < built.pieces.size(); ++i)
        {
            if (ids.contains(built.of[i].held))
                out.push_back(std::move(built.pieces[i]));
        }
        return out;
    }

    std::unordered_set<std::uint64_t> supports_passed(const ParkScene& scene)
    {
        std::unordered_set<std::uint64_t> platforms;
        for (const save::model::Ride& ride : scene.rides.rides)
        {
            for (const save::model::TrackPiece& piece : ride.track ? ride.track->pieces : std::vector<save::model::TrackPiece> { })
            {
                if (piece.platform)
                    platforms.insert(piece.platform->id);
            }
        }
        // A queue's railings stand on its deck: they hold up nothing and are not in the way of its columns.
        std::set<std::pair<std::string_view, std::string_view>> railings;
        for (const save::model::PathTile& p : scene.footpaths.paths)
        {
            if (p.queue)
                railings.emplace(p.queue->fence_symbol, p.queue->fence_overlay);
        }
        std::unordered_set<std::uint64_t> out(scene.footpaths.paving_decks.begin(), scene.footpaths.paving_decks.end());
        // A ride's fence stands on its floor's rim: it holds up nothing and is not in the way of the ride's columns.
        for (const auto& [_, pieces] : scene.ride_fence_pieces)
            out.insert(pieces.begin(), pieces.end());
        for (const save::model::PlacedObject& o : scene.objects)
        {
            if ((o.owner_ref != o.id && platforms.contains(o.owner_ref)) || (o.fixture && railings.contains({ o.symbol, o.overlay })))
                out.insert(o.id);
        }
        return out;
    }

} // namespace rct3::scene::park
