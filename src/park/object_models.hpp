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
#include "ovl/ride_train.hpp"
#include "park/asset_index.hpp"
#include "park/bone_poses.hpp"
#include "park/flexi_colours.hpp"
#include "park/item_category.hpp"
#include "park/model_lights.hpp"
#include "park/ride_walks.hpp"
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
    struct SceneryVisual;
    struct StaticShape;
} // namespace rct3::ovl

namespace rct3::park
{

    /// How a part is blended with what is behind it.
    enum class PartBlend : std::uint8_t
    {
        Opaque,
        Cutout,  ///< Pixels are either drawn or not, by their alpha
        Blended, ///< See-through, drawn after everything solid
        /// Writes depth only, after everything solid and before water and see-through parts, so what those draw
        /// behind it stays hidden: a boat's lid keeps the water its hull sits in out of the boat.
        DepthOnly,
    };

    /// How a mesh with `transparency` (0 opaque, 1 cut out, 2 blended) and texture style `style` is blended. A
    /// style that fills depth (SIFillZ) draws depth only, whatever its transparency says.
    [[nodiscard]] PartBlend part_blend(std::uint32_t transparency, std::string_view style);

    struct ModelVertex
    {
        glm::vec3 position { 0.0f };
        glm::vec3 normal { 0.0f, 1.0f, 0.0f };
        glm::vec2 uv { 0.0f };
        /// Of a model moved by bones, at its rig's level: up to four bones of the rig that move the vertex, and
        /// how much each does, out of 255. Zero weights mean nothing.
        std::array<std::uint8_t, 4> bones { };
        std::array<std::uint8_t, 4> weights { };
    };

    struct ModelPart
    {
        std::string texture;     ///< Key of the part's picture, empty for none
        std::uint32_t slots = 0; ///< Colour slots the picture recolours (bit k: slot k)
        PartBlend blend = PartBlend::Opaque;
        bool one_sided = false; ///< Only the side its triangles face is drawn; the file marks the rest as both
        std::vector<ModelVertex> vertices;
        std::vector<std::uint32_t> indices; ///< Three per triangle
        /// Of a car on an arm, at every level: the arm's joint that carries it, or -1 for the car itself.
        std::int32_t bone = -1;
        bool board = false; ///< A board for words (ovl::kMeshFlagBoard)
        bool chain = false; ///< A lift piece's chain, which moves along its picture as the lift runs (chain_lifts.hpp)
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

    /// How a car sits on its track, in its own space, where it faces +z.
    struct CarFit
    {
        float length = 0;       ///< From its front to its rear, which is how far apart cars of a train stand
        float front_wheels = 0; ///< How far behind its front its front wheels are
        float rear_wheels = 0;  ///< How far behind its front its rear wheels are
        /// The point midway between its front and rear wheels, which stands on the track.
        glm::vec3 on_track { 0.0f };
    };

    /// The cameras a car may have: on its nose, which only the train's first car shows, at its front seat
    /// and at its rear seat. The rear one never looks ahead.
    enum class RideEye : std::uint8_t
    {
        Nose,
        Front,
        Rear,
    };

    struct ObjectModel
    {
        std::uint16_t positioning = 0;                   ///< How the item sits on its tile (see object_placement.hpp)
        std::uint32_t category = 0;                      ///< What kind of item it is (see item_category.hpp)
        std::array<std::int32_t, 2> squares { 1, 1 };    ///< Tiles it covers along its own X and Z
        float sway = 0;                                  ///< How much it sways in the wind; above 0 for trees and plants only
        std::string support_group;                       ///< The set of supports it stands on, or the set a support belongs to
        std::int32_t lowest_level = 0;                   ///< Lowest level its footprint takes, from its base height
        bool water_only = false;                         ///< A ride that stands only on water
        bool fenced_round = false;                       ///< A track piece fenced round its footprint, a tower's base
        bool paved_under = false;                        ///< A track piece whose footprint is paved, a tower's base
        bool photo_spot = false;                         ///< A path item guests stop at to have their photo taken
        bool no_entry = false;                           ///< A sign that can be set to forbid entry
        bool ground_height = true;                       ///< On the ground, it stands at the ground's height there
        bool fits_ground = false;                        ///< A plant on the ground, its shape bends to follow it
        bool level_paths_only = false;                   ///< A path item kept off the sides of sloped paths
        bool holds_ground = false;                       ///< A ride, stall or the like the ground under is not reshaped
        std::array<std::int32_t, 3> default_colours { }; ///< Colours it takes when nothing names its own
        /// For rides fenced round an outline, not their whole footprint: per square, row by row (X fastest),
        /// whether it is inside the outline. Empty for every other item.
        std::vector<bool> fence_outline;
        std::optional<SupportFootprint> supports; ///< What it asks of the columns under it
        /// Per square, row by row (X fastest): the lowest level over its base it fills solid and the one over the highest, or
        /// nothing where nothing collides.
        std::vector<std::optional<std::array<std::int32_t, 2>>> solid;
        std::vector<ModelLevel> levels; ///< Finest first, never empty
        /// How it looks once vandals have broken it, level for level as `levels`: the visual the item lists
        /// named after it with "_Broken" (a bench has one, a bin or a lamp not). Empty when it has none.
        std::vector<ModelLevel> broken_levels;
        std::int32_t price = 0;        ///< To build one, hundredths of the currency unit
        std::int32_t removal_cost = 0; ///< To pull one down, hundredths; below zero it pays back
        /// Of an animated model, the length in seconds of each animation its finest level lists, in the order
        /// listed; 0 for one that cannot be read. Flat rides list theirs as standing, starting, running and
        /// stopping.
        std::vector<float> animation_seconds;
        std::vector<std::string> animation_names; ///< Of those animations, in the same order
        /// Places for riders: the most bones named "peep" and a number that any level has.
        std::uint32_t seats = 0;
        /// Where each rider sits, model space, by seat number, from the first level with seats.
        std::vector<glm::mat4> seat_places;
        /// Of a ride car, how its riders sit (ovl::RideCar::kind's low byte); nothing when the ride says.
        std::optional<std::uint8_t> seating;
        /// Of a model its bones move: the bones and their motions, from the first level listing motions,
        /// whose vertices each name the bones that move them.
        std::optional<BoneRig> rig;
        std::size_t rig_level = 0;
        std::vector<std::int32_t> seat_bones; ///< By seat number, the rig's bone the seat is, when it has one
        /// Of a flat ride, its own fixed cameras, bones named "camera" and a number: model space, by number,
        /// looking along +Z with +Y up, and the rig's bone each is, when it has one.
        std::vector<glm::mat4> view_places;
        std::vector<std::int32_t> view_bones;
        /// Of a car whose riders sit on an arm: the arm's joints at rest, model space, from the base out. The
        /// first turns about its own up, the others about their own sideways.
        std::vector<glm::mat4> arm_joints;
        /// Of a car with bones named "carfront" and "carrear" at its ends: how it sits on its track. Its wheels
        /// are its wheel bones, or both midway between its ends, on its own centre line, where it has none.
        std::optional<CarFit> car_fit;
        /// Of a car a rider can look out of: the eyes of each kind of camera it has, by RideEye, model space,
        /// looking along +Z with +Y up, as its camera bones have them. A car hung under its track has them
        /// turned over. Its nose's are the front seat's where it has no nose camera.
        std::array<std::optional<glm::mat4>, 3> ride_eyes;
        ovl::RideCameraTuning ride_look; ///< How they move, as the car's train sets it
        /// Of a flat ride, the walkways its riders take to and from their seats, model space; empty for most.
        Walkways walkways;
        /// The lamps it carries, from the finest level that marks any (see model_lights.hpp). A lamp on a bone
        /// names the rig's bone, or the arm's joint for a car on an arm.
        std::vector<ModelLight> lights;
        /// The particle effects it shows, found as its lamps are.
        std::vector<ModelEffect> effects;
        /// Of a stall or a ride's entrance, where its seller stands, model space, facing +Z: its point or bone
        /// named "vendor".
        std::optional<glm::mat4> vendor_place;
    };

    /// The colour slots any picture of `model` recolours, at any level (bit k: slot k).
    [[nodiscard]] std::uint32_t colour_slots(const ObjectModel& model);

    /// How far an item's levels reach, as shares of the distances its model gives them: `detail` for every level
    /// but the last, so how long the finer levels hold, and `distance` for the last, beyond which it is not drawn.
    struct LevelScale
    {
        float detail = 1;
        float distance = 1;
    };
    /// By DetailKind.
    using LevelScales = std::array<LevelScale, kDetailKinds>;

    /// How far level `level` of `model` reaches with `scale`; never past where the last level ends.
    [[nodiscard]] float level_reach(const ObjectModel& model, std::size_t level, LevelScale scale);

    /// The level of `model` to draw at `distance` with `scale`: the first that reaches that far. Nothing when the
    /// item is too far away for all of them.
    [[nodiscard]] std::optional<std::size_t> level_at(const ObjectModel& model, float distance, LevelScale scale = { });

    using ModelPictures = std::map<std::string, PaletteImage, std::less<>>;

    /// Key of the picture given to parts drawn as water: their texture style is SIWater and the texture they
    /// name is in no file, so the look is ours.
    inline constexpr std::string_view kWaterPicture = "rct3pp:water";

    /// Key of the picture given to chrome parts: their look is the surroundings mirrored in them, which the
    /// scene draws from the install's chrome picture, whatever texture they name (most name none in any file).
    inline constexpr std::string_view kChromePicture = "rct3pp:chrome";

    /// Whether texture style `style` draws a mirror-like chrome surface whose own texture is not read
    /// (SIOpaqueChrome). The kinds that read theirs, for holes (SIAlphaMaskChrome) or to multiply
    /// (SIOpaqueChromeModulate), are not: they draw as that texture.
    [[nodiscard]] bool chrome_style(std::string_view style);

    /// Keys given to parts painted like the ground they stand on, such as the terrain blocks: they name no
    /// picture of their own but these two, one for the land's surface and one for its cliffs. The object mesh
    /// gives them the pictures of the tile under the object (see object_mesh.hpp).
    inline constexpr std::string_view kGroundSurfaceTexture = "useterraintexture";
    inline constexpr std::string_view kGroundCliffTexture = "useclifftexture";
    inline constexpr std::string_view kGroundSurfacePicture = "rct3pp:ground surface";
    inline constexpr std::string_view kGroundCliffPicture = "rct3pp:ground cliff";

    /// How far those parts are lifted: they lie on the basin they fill, and would fight it for depth. Our own
    /// value, small enough not to show.
    inline constexpr float kWaterLift = 0.03f;

    /// A see-through, faintly rippled picture for those parts, in the colour of the park's lakes.
    [[nodiscard]] PaletteImage water_picture();

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
        /// its first. With no such item, the visual of that name, drawn as it is. Nothing when the item or
        /// every level it could show is missing.
        [[nodiscard]] std::optional<ObjectModel> load(std::string_view symbol, std::string_view overlay, bool modelled_plants = true);

        /// The model of car `slot` (0 to 5) of the ride train `train` (its name without the tag), looked for in
        /// `overlay` first. Nothing when the train, the car or every level of its visual is missing.
        [[nodiscard]] std::optional<ObjectModel> load_car(std::string_view train, std::string_view overlay, std::size_t slot);

        /// A model of the one static shape `shape` (its name without the tag), as carried items are drawn,
        /// looked for in `overlay` first. Nothing when it is missing.
        [[nodiscard]] std::optional<ObjectModel> load_shape(std::string_view shape, std::string_view overlay);

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
        /// Gives `part` the picture of texture `name` near `near`, or the water picture when `style` asks for it.
        void set_texture(ModelPart& part, std::string_view name, std::string_view style, std::string_view near);
        /// Adds the levels of `visual`, held in the file at `visual_path`, to `model`.
        void add_levels(ObjectModel& model, const ovl::SceneryVisual& visual, std::string_view visual_path);
        /// Adds the meshes of static shape `name`, looked for near `near`, to `level`; false when it is missing.
        const ovl::StaticShape* add_static(ModelLevel& level, std::string_view name, std::string_view near);
        /// Adds to every level of `model` the water running down it, when it is a track piece of the file at
        /// `path` that carries water (see water_flow.hpp).
        void add_flow(ObjectModel& model, std::string_view symbol, std::string_view path);
    };

} // namespace rct3::park
