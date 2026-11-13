// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/objects.hpp"

#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/object/models.hpp"
#include "profiling/profiler.hpp"
#include "scene/park/particles.hpp"
#include "scene/park/scene.hpp"
#include "ui/interface_assets.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace rct3::scene
{

    TrackModels::TrackModels() = default;
    TrackModels::~TrackModels() = default;
    TrackModels::TrackModels(TrackModels&&) noexcept = default;
    TrackModels& TrackModels::operator=(TrackModels&&) noexcept = default;

    std::string slashes(std::string path)
    {
        std::ranges::replace(path, '\\', '/');
        return path;
    }

    bool load_object_model(park::ParkScene& scene, std::map<std::string, TrackModels, std::less<>>& cache, const ui::InterfaceAssets& assets, const save::model::PlacedObject& object)
    {
        const std::string key = rct3::park::object::model_key(object);
        if (scene.models.contains(key))
            return true;
        if (!assets.install)
            return false;
        auto [found, added] = cache.try_emplace(object.overlay);
        TrackModels& m = found->second;
        // Adds `file` and the overlays it takes assets from to the index.
        const auto add_with_references = [&](const std::filesystem::path& file) {
            const bool fresh = m.index->add_file(file);
            std::set<std::string, std::less<>> tried;
            for (auto missing = m.index->missing_references(); !missing.empty(); missing = m.index->missing_references())
            {
                bool more = false;
                for (const std::string& reference : missing)
                {
                    if (!tried.insert(reference).second)
                        continue;
                    if (const auto found_file = assets.install->find(std::format("{}.common.ovl", reference)))
                        more = m.index->add_file(*found_file) || more;
                }
                if (!more)
                    break;
            }
            return fresh;
        };
        if (added)
        {
            // Only the overlay and those it takes assets from: scanning the whole track folder would be slow.
            m.index = std::make_unique<rct3::park::AssetIndex>(rct3::park::AssetIndex::scan(assets.install->root(), { }));
            if (const auto file = assets.install->find(std::format("{}.common.ovl", slashes(object.overlay))))
                (void)add_with_references(*file);
            m.loader = std::make_unique<rct3::park::object::ModelLoader>(*m.index);
        }
        if (m.missing.contains(object.symbol))
            return false;
        auto model = m.loader->load(object.symbol, object.overlay, scene.modelled_plants);
        if (!model)
        {
            // Path and queue styles and support sets keep each piece in an overlay of its own, named after the
            // piece, which the describing overlay does not refer to: beside it, or in a folder beside it.
            const std::string overlay = slashes(object.overlay);
            const std::string folder = overlay.substr(0, overlay.rfind('/') + 1);
            const std::string file_name = std::format("{}.common.ovl", object.symbol);
            std::optional<std::filesystem::path> file = assets.install->find(folder + file_name);
            if (const auto dir = assets.install->find(folder.empty() ? "." : folder); !file && dir)
            {
                std::error_code error;
                for (const auto& entry : std::filesystem::directory_iterator(*dir, error))
                {
                    if (entry.is_directory(error))
                        file = assets.install->find(std::format("{}{}/{}", folder, entry.path().filename().string(), file_name));
                    if (file)
                        break;
                }
            }
            if (file && add_with_references(*file))
            {
                m.loader = std::make_unique<rct3::park::object::ModelLoader>(*m.index);
                model = m.loader->load(object.symbol, object.overlay, scene.modelled_plants);
            }
            // A theme keeps its rides and entrances in overlays of their own, named apart from the items they
            // hold, below its folder (Rides/<ride>/, EntranceExit/<style>/) or, for some, below the folder above
            // it (a sibling Rides/<theme>Rides/): the last resort reads all of those, each folder once.
            std::string up = folder;
            for (int level = 0; level < 2 && !model && !up.empty(); ++level, up = up.substr(0, up.rfind('/', up.size() - 2) + 1))
            {
                const auto dir = assets.install->find(up);
                if (!dir || !m.folders_read.insert(up).second)
                    continue;
                bool added = false;
                std::error_code error;
                for (auto it = std::filesystem::recursive_directory_iterator(*dir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                {
                    if (it->is_regular_file(error) && it->path().filename().string().ends_with(".common.ovl"))
                        added = add_with_references(it->path()) || added;
                }
                if (added)
                {
                    m.loader = std::make_unique<rct3::park::object::ModelLoader>(*m.index);
                    model = m.loader->load(object.symbol, object.overlay, scene.modelled_plants);
                }
            }
        }
        if (!model)
            m.missing.insert(object.symbol);
        if (!model)
            return false;
        wait_for_build(scene); // The build beside the frame reads the models
        scene.models.emplace(key, std::move(*model));
        for (const auto& [name, picture] : m.loader->pictures())
            scene.pictures.try_emplace(name, picture);
        return true;
    }

    void objects_changed(park::ParkScene& scene)
    {
        const profiling::Scope zone("ObjectsChanged");
        scene.supports.stale = true;
        wait_for_build(scene); // The build beside the frame reads the anchors written here
        profiling::section("Places", [&] { place_objects(scene); });
        const rct3::park::object::ObjectPlaces& places = scene.places;
        scene.anchors.resize(places.transforms.size());
        std::ranges::transform(places.transforms, scene.anchors.begin(), [](const glm::mat4& m) { return glm::vec3(m[3]); });
        scene.lamps = profiling::section("Lamps", [&] { return rct3::park::object::object_lamps(scene.objects, scene.models, scene.grid, scene.fitted, places.transforms, places.models); });
        scene.lamps_due = true;
        profiling::section("Effects", [&] { park::show_park_particles(scene.particles, rct3::park::object::object_effects(scene.objects, scene.models, scene.grid, scene.fitted, places.transforms, places.models)); });
        scene.levels = { };
        scene.since_rebuild = park::kObjectRebuildInterval;
        scene.chairs_changed = true;
    }

    bool replace_fixtures(park::ParkScene& scene, std::vector<std::uint64_t>& ids, std::vector<save::model::PlacedObject> pieces)
    {
        const auto look = [](const save::model::PlacedObject& o) { return std::tie(o.overlay, o.symbol, o.col, o.row, o.base_height, o.rotation, o.quadrant, o.height_adjust, o.colours, o.height_offset, o.frame_offset, o.force_absolute_height, o.hidden, o.fixture); };
        std::map<std::tuple<std::int32_t, std::int32_t, std::string_view>, std::vector<std::size_t>> by_spot;
        for (std::size_t k = 0; k < pieces.size(); ++k)
            by_spot[{ pieces[k].col, pieces[k].row, pieces[k].symbol }].push_back(k);
        const std::unordered_set<std::uint64_t> old(ids.begin(), ids.end());
        std::vector<bool> kept(pieces.size(), false);
        std::vector<std::size_t> free;
        ids.clear();
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            const save::model::PlacedObject& o = scene.objects[i];
            if (!old.contains(o.id))
                continue;
            const auto here = by_spot.find({ o.col, o.row, o.symbol });
            const std::span<const std::size_t> alike = here != by_spot.end() ? std::span<const std::size_t>(here->second) : std::span<const std::size_t>();
            const auto same = std::ranges::find_if(alike, [&](std::size_t k) { return !kept[k] && look(pieces[k]) == look(o); });
            if (same == alike.end())
            {
                free.push_back(i);
                continue;
            }
            kept[*same] = true;
            ids.push_back(o.id);
        }
        bool changed = false;
        auto slot = free.begin();
        for (std::size_t k = 0; k < pieces.size(); ++k)
        {
            if (kept[k])
                continue;
            changed = true;
            ids.push_back(pieces[k].id);
            if (slot != free.end())
                scene.objects[*slot++] = std::move(pieces[k]);
            else
                scene.objects.push_back(std::move(pieces[k]));
        }
        if (slot != free.end())
        {
            changed = true;
            std::unordered_set<std::uint64_t> gone;
            for (; slot != free.end(); ++slot)
                gone.insert(scene.objects[*slot].id);
            std::erase_if(scene.objects, [&](const save::model::PlacedObject& o) { return gone.contains(o.id); });
        }
        return changed;
    }

    void place_objects(const park::ParkScene& scene)
    {
        const std::vector<std::size_t> changed = rct3::park::object::update_places(scene.places, scene.objects, scene.models, scene.grid, scene.fitted);
        rct3::park::object::update_pick_boxes(scene.pick_boxes, scene.places, changed);
        scene.pick_boxes_of = scene.objects.size();
    }

    void wait_for_build(park::ParkScene& scene)
    {
        if (scene.object_build.valid())
            (void)scene.object_build.get();
    }

} // namespace rct3::scene
