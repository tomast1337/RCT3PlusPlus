// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D models of the things placed in a park. An asset's scenery item lists its visuals; a visual lists
// levels of detail, each a static shape, a bone shape or a flat billboard. Trees list a billboard visual
// first and the modelled one after it. The model is the first level of the first visual that has a shape,
// with a bone shape in its resting pose. Each of its parts is painted with one texture, by name; its
// picture is kept as palette indices, as a placed object may recolour it (see flexi_colours.hpp).
//
// The models' coordinates are the game's: Y up, the ground at 0, a full-tile item centred on its tile.
#pragma once

#include "math/glm.hpp"
#include "park/asset_index.hpp"
#include "park/flexi_colours.hpp"

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

    struct ObjectModel
    {
        std::uint16_t positioning = 0; ///< How the item sits on its tile (see object_placement.hpp)
        std::vector<ModelPart> parts;
    };

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
        /// park names (`overlay`) first. Nothing when the item or every shape it could show is missing.
        [[nodiscard]] std::optional<ObjectModel> load(std::string_view symbol, std::string_view overlay);

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
