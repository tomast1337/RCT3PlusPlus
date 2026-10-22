// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's placed objects as meshes: every visible object's model, at the level of detail its distance
// from the viewer calls for, moved to where it stands, lit by the same fixed sun as the ground, and grouped
// into batches by picture, colours and blending. Batches come opaque first, then cut-out, then
// see-through, which is the order to draw them in. Shapes and billboards are built apart: shapes only
// change when an object's level does, billboards whenever the viewer moves. Which billboards there are and
// how they are batched also only changes with the levels, so billboards are gathered then, and only turned
// when the viewer moves.
//
// Parts painted like the ground (kGroundSurfacePicture, kGroundCliffPicture) take the surface or cliff picture
// of the tile under the object's anchor, by its ground key (ground_textures.hpp), and are laid on it as the
// terrain is: tops by their place on the map, sides by their place along the face and their height, so they
// meet the ground round them without a seam.
#pragma once

#include "park/lamp_grid.hpp"
#include "park/object_models.hpp"
#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// A run of `count` indices from `first` painted with one picture in one set of colours, blended one way.
    struct ObjectBatch
    {
        std::string texture; ///< The painted picture's name (see painted_name)
        std::string picture; ///< Key of the picture it is painted from
        FlexiColours colours { };
        PartBlend blend = PartBlend::Opaque;
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        bool one_sided = false; ///< Drawn only from the side its triangles face (see ModelPart)
    };

    struct ObjectMesh
    {
        std::vector<GroundVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<ObjectBatch> batches;
        std::vector<std::uint32_t> owners; ///< By vertex, the index of the object it belongs to
        std::size_t placed = 0;            ///< Objects drawn
        std::size_t missing = 0;           ///< Visible objects without a model
    };

    using ObjectModels = std::map<std::string, ObjectModel, std::less<>>;
    /// Colours to paint some objects in instead of the ones they picked, by object id.
    using ColourOverrides = std::map<std::uint64_t, FlexiColours>;
    /// How much some objects' shapes rise per park unit along x and z away from their anchors, by object
    /// id, so a straight piece such as a fence panel follows a slope. Upright lines stay upright.
    using Leans = std::map<std::uint64_t, glm::vec2>;
    /// The whole transform from model to park of some objects, by object id, for built pieces placed off
    /// the tile grid, turned any way or stretched (see track_supports.hpp). Others stand by their tile.
    using Fitted = std::map<std::uint64_t, glm::mat4>;

    /// The level each object is drawn at, by its index; kNotDrawn for hidden objects, objects without a model
    /// and objects too far away.
    using ObjectLevels = std::vector<std::int8_t>;
    inline constexpr std::int8_t kNotDrawn = -1;

    /// The key `object`'s model is kept by: its overlay and its symbol.
    [[nodiscard]] std::string model_key(const save::PlacedObject& object);

    /// Where `object` stands in the world, model to world: as `fitted` has it, else as its model places it.
    [[nodiscard]] glm::mat4 placed_transform(const save::PlacedObject& object, const ObjectModel* model, const save::TerrainGrid& grid, const Fitted& fitted = { });

    /// Where each object's model origin lands in the park, by index.
    [[nodiscard]] std::vector<glm::vec3> object_anchors(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted = { });

    /// The lamps the objects' models carry, placed in the park, in the colour each names or each object's slot
    /// gives, at full strength. Hidden objects are left out.
    /// shortcut: lamps on bones stand where the bone rests, so a turning ride's lamps stay put; move them with
    /// the bones once ride lighting at night shows it.
    [[nodiscard]] std::vector<Lamp> object_lamps(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted = { });

    /// The level of each object seen from `eye`, with the models' distances times `scale`.
    [[nodiscard]] ObjectLevels object_levels(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, const glm::vec3& eye, float scale = 1);

    /// How far round a crossing objects are already drawn as crossing, as a share of its distance, so ones the
    /// eye reaches between two looks at the levels are ready.
    inline constexpr float kCrossMargin = 0.1f;

    /// The levels objects are drawn at from some eye. Objects near the edge of a level are drawn at two levels
    /// at once, and cross from the finer to the coarser as the eye's distance runs through their band.
    struct LevelPlan
    {
        ObjectLevels steady;              ///< Drawn whole at one level; kNotDrawn for crossing objects
        ObjectLevels finer;               ///< Crossing objects at their finer level, kNotDrawn for the others
        ObjectLevels coarser;             ///< At their coarser level; kNotDrawn too past the last level
        std::vector<glm::vec2> bands { }; ///< By object: the distances its crossing starts and ends at

        bool operator==(const LevelPlan&) const = default;
    };

    /// The plan for `objects` seen from `eye`, with the models' distances times `scale`. An object starts crossing
    /// into the next level `band` of a level's farthest distance before it, as the eye draws away, and is wholly
    /// at the next level at the distance itself; with `band` 0, objects jump from level to level.
    [[nodiscard]] LevelPlan plan_levels(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, const glm::vec3& eye, float band, float scale = 1);

    /// The shapes of the objects at `levels` (by index; when it does not cover every object, each is drawn at
    /// its finest level), leaning those in `leans`. Billboard levels are left out.
    [[nodiscard]] ObjectMesh object_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, std::span<const std::int8_t> levels = { }, const ColourOverrides& overrides = { }, const Leans& leans = { }, const Fitted& fitted = { });

    /// Where a billboard stands and how big it is. Its four corners are the next four vertices of the mesh.
    struct BillboardStand
    {
        glm::vec3 base { 0.0f }; ///< Middle of its bottom edge
        float half_width = 0;
        float height = 0;
    };

    /// Billboards batched and painted, waiting to be turned to a viewer.
    struct Billboards
    {
        ObjectMesh mesh;                    ///< Corners are placed by face_billboards
        std::vector<BillboardStand> stands; ///< In the order of the mesh's corners
    };

    /// The billboards of the objects at `levels`, each standing on its anchor, not yet turned.
    [[nodiscard]] Billboards gather_billboards(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, std::span<const std::int8_t> levels, const ColourOverrides& overrides = { });

    /// Places the corners of `boards` so each faces `eye`, turning only round the upright.
    void face_billboards(Billboards& boards, const glm::vec3& eye);

    /// The billboards of the objects at `levels`, each standing on its anchor and turned to face `eye`.
    [[nodiscard]] ObjectMesh billboard_mesh(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const glm::vec3> anchors, std::span<const std::int8_t> levels, const glm::vec3& eye, const ColourOverrides& overrides = { });

    /// The painted pictures `batches` draw with, by their names. Batches whose picture is missing get none.
    [[nodiscard]] std::map<std::string, ovl::RgbaImage, std::less<>> paint_batches(std::span<const ObjectBatch> batches, const ModelPictures& pictures);

} // namespace rct3::park
