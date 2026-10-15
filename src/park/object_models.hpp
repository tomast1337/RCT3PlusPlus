// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D models of the things placed in a park. An asset's scenery item lists its visuals; a visual lists
// levels of detail, from the finest, each a static shape, a bone shape or a flat billboard, and each with
// the farthest distance it is drawn at. Trees and plants list a flat visual first and a modelled one after
// it, which lives in folders of their own: with 3D plants on the model takes the last visual that can be
// read, otherwise the first. Bone shapes are in their resting pose. Each part is painted with one texture,
// by name; its picture is kept as palette indices, as a placed object may recolour it (see
// flexi_colours.hpp).
//
// The models' coordinates are the game's: Y up, the ground at 0, a full-tile item centred on its tile.
#pragma once

#include "math/glm.hpp"
#include "park/asset_index.hpp"
#include "park/flexi_colours.hpp"
#include "park/support_footprint.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ovl
{
    class Overlay;
}

namespace rct3::park
{

    /// How a part is blended with what is behind it.
    enum class PartBlend : std::uint8_t
    {
        Opaque,
        Cutout,  ///< Pixels are either drawn or not, by their alpha
        Blended, ///< See-through, drawn after everything solid
    };

    struct ModelVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec3 normal { 0.0f, 1.0f, 0.0f };
        glm::vec2 uv { 0.0f };
    };

    struct ModelPart
    {
        std::string texture;     ///< Key of the part's picture, empty for none
        std::uint32_t slots = 0; ///< Colour slots the picture recolours (bit k: slot k)
        PartBlend blend = PartBlend::Opaque;
        std::vector<ModelVertex> vertices;
        std::vector<std::uint32_t> indices; ///< Three per triangle
    };

    /// A flat picture standing upright on the item's anchor, turned to face the viewer round the vertical.
    struct Billboard
    {
        std::string texture;     ///< Key of the picture, empty for none
        std::uint32_t slots = 0; ///< Colour slots the picture recolours
        float width = 0;
        float height = 0;
        glm::vec2 uv_min { 0.0f }; ///< Texture coordinates of the top left corner
        glm::vec2 uv_max { 1.0f }; ///< And of the bottom right
    };

    /// One level of detail: shape parts, or a billboard.
    struct ModelLevel
    {
        float distance = 0; ///< Farthest distance from the viewer this level is drawn at
        std::vector<ModelPart> parts;
        std::optional<Billboard> billboard;
    };

    struct ObjectModel
    {
        std::uint16_t positioning = 0;                ///< How the item sits on its tile (see object_placement.hpp)
        std::array<std::int32_t, 2> squares { 1, 1 }; ///< Tiles it covers along its own X and Z
        float sway = 0;                               ///< How much it sways in the wind; above 0 for trees and plants only
        std::string support_group;                    ///< The set of supports it stands on, or the set a support belongs to
        std::int32_t lowest_level = 0;                ///< Lowest level its footprint takes, from its base height
        std::optional<SupportFootprint> supports;     ///< What it asks of the columns under it
        std::vector<ModelLevel> levels;               ///< Finest first, never empty
    };

    /// The level of `model` to draw at `distance`, with every level's distance times `scale`: the first that
    /// reaches that far. Nothing when the item is too far away for all of them.
    [[nodiscard]] std::optional<std::size_t> level_at(const ObjectModel& model, float distance, float scale = 1);

    using ModelPictures = std::map<std::string, PaletteImage, std::less<>>;

    /// Loads models through an asset index, reading each overlay once. Keep it while loading a park's models,
    /// then drop it to free the files.
    class ModelLoader
    {
    public:
        explicit ModelLoader(const AssetIndex& index);
        ~ModelLoader();
        ModelLoader(const ModelLoader&) = delete;
        ModelLoader& operator=(const ModelLoader&) = delete;

        /// The model of the scenery item `symbol` (its name without the tag), looked for in the overlay the
        /// park names (`overlay`) first. `modelled_plants` picks the item's last readable visual instead of
        /// its first. Nothing when the item or every level it could show is missing.
        [[nodiscard]] std::optional<ObjectModel> load(std::string_view symbol, std::string_view overlay, bool modelled_plants = true);

        /// The pictures the loaded models' parts name, by key.
        [[nodiscard]] const ModelPictures& pictures() const
        {
            return pictures_;
        }

    private:
        struct Files;

        const AssetIndex& index_;
        std::unique_ptr<Files> files_;
        ModelPictures pictures_;
        std::map<std::string, bool, std::less<>> tried_pictures_;

        std::string texture_key(std::string_view name, std::string_view near);
        void set_texture(ModelPart& part, std::string_view name, std::string_view near);
    };

} // namespace rct3::park
