// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's objects as the build tools change them: the models of the objects they place, loaded through their
// overlay's own index as they are first shown, and the scene's object list kept up to date after a change. Track,
// path, pool and structure building all place objects this way.
#pragma once

#include "park/object/models.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace rct3::park
{
    class AssetIndex;
} // namespace rct3::park

namespace rct3::ui
{
    struct InterfaceAssets;
} // namespace rct3::ui

namespace rct3::scene
{

    namespace park
    {
        struct ParkScene;
    }

    /// The models of one track overlay's pieces, loaded as they are first shown.
    struct TrackModels
    {
        std::unique_ptr<rct3::park::AssetIndex> index;
        std::unique_ptr<rct3::park::object::ModelLoader> loader;
        std::set<std::string, std::less<>> missing;      ///< Symbols looked for and not found, so not looked for again
        std::set<std::string, std::less<>> folders_read; ///< Folders every overlay below which was added

        TrackModels();
        ~TrackModels();
        TrackModels(TrackModels&&) noexcept;
        TrackModels& operator=(TrackModels&&) noexcept;
    };

    /// `path` with its folders split by '/' where the game's files write '\'.
    [[nodiscard]] std::string slashes(std::string path);

    /// Makes sure the model of `object` is loaded, reading it through its overlay's own index kept in `cache`.
    bool load_object_model(park::ParkScene& scene, std::map<std::string, TrackModels, std::less<>>& cache, const ui::InterfaceAssets& assets, const save::model::PlacedObject& object);

    /// Hands the scene's objects to the mesh builder again after a change, once a build reading them is done.
    void objects_changed(park::ParkScene& scene);

    /// Puts `pieces` in the place of the objects `ids` names, which then names the pieces. A piece the same as
    /// one standing keeps it, with its id and its place among the objects; the others take the places of those
    /// that went, so the objects after them do not move. Whether any object changed.
    bool replace_fixtures(park::ParkScene& scene, std::vector<std::uint64_t>& ids, std::vector<save::model::PlacedObject> pieces);

    /// Brings the objects' places and pick boxes up to date, working out again only the objects that changed.
    void place_objects(const park::ParkScene& scene);

    /// Waits for a mesh build that is reading the objects, before they change.
    void wait_for_build(park::ParkScene& scene);

} // namespace rct3::scene
