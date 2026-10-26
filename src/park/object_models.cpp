// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_models.hpp"

#include "ovl/bone_anim.hpp"
#include "ovl/bone_shape.hpp"
#include "ovl/flexi_texture.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ride_car.hpp"
#include "ovl/ride_train.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/scenery_visual.hpp"
#include "ovl/spline.hpp"
#include "ovl/static_shape.hpp"
#include "ovl/track_section.hpp"
#include "park/item_category.hpp"
#include "park/terrain_mesh.hpp"
#include "park/track_pieces.hpp"
#include "park/water_flow.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <ranges>
#include <span>
#include <utility>

namespace rct3::park
{

    namespace
    {

        constexpr std::string_view kItemTag = ":sid";
        /// A footprint square's flag for a square things collide with.
        constexpr std::uint32_t kSquareSolid = 0x1;
        constexpr std::string_view kVisualTag = ":svd";
        constexpr std::string_view kStaticTag = ":shs";
        constexpr std::string_view kBoneTag = ":bsh";
        constexpr std::string_view kAnimTag = ":ban";
        constexpr std::string_view kTextureTag = ":ftx";
        constexpr std::string_view kTrainTag = ":rit";
        constexpr std::string_view kCarTag = ":ric";

        /// Entries of an overlay by symbol name, whatever its case: items name visuals, shapes and textures
        /// without always spelling them as the files that hold them do.
        template<typename T> using ByName = std::map<std::string_view, T, strings::LessIgnoringCase>;

        /// One overlay and what has been read from it, by symbol name. Views point into the overlay.
        struct OpenFile
        {
            ovl::Overlay overlay;
            std::optional<ByName<ovl::SceneryItem>> items;
            std::optional<ByName<ovl::SceneryVisual>> visuals;
            std::optional<ByName<ovl::StaticShape>> statics;
            std::optional<ByName<ovl::BoneShape>> bones;
            std::optional<ByName<ovl::BoneAnim>> anims;
            std::optional<ByName<ovl::FlexiTexture>> textures;
            std::optional<ByName<ovl::RideTrain>> trains;
            std::optional<ByName<ovl::RideCar>> cars;
            std::optional<ByName<ovl::TrackSection>> sections;
            std::optional<ByName<ovl::Spline>> splines;
        };

        /// Every readable entry of both parts, read the first time it is asked for.
        template<typename T, typename Read> const ByName<T>& entries(std::optional<ByName<T>>& cache, const ovl::Overlay& overlay, Read&& read)
        {
            if (!cache)
            {
                cache.emplace();
                for (const auto part : { maybe(&overlay.common()), overlay.unique() })
                {
                    if (!part)
                        continue;
                    for (auto& [name, value] : read(overlay, *part))
                    {
                        if (value)
                            cache->emplace(name, std::move(*value));
                    }
                }
            }
            return *cache;
        }

        template<typename T> const T* lookup(const ByName<T>& map, std::string_view name)
        {
            const auto it = map.find(name);
            return it == map.end() ? nullptr : &it->second;
        }

        PartBlend blend_of(std::uint32_t transparency)
        {
            switch (transparency)
            {
                case 1:
                    return PartBlend::Cutout;
                case 2:
                    return PartBlend::Blended;
                default:
                    return PartBlend::Opaque;
            }
        }

        /// A mesh's sides value for drawing only the front of its triangles; 1 draws both. Of the shapes in the
        /// install, those marked so face the way their normals point all but 0.2% of the time, the rest 0.4.
        constexpr std::uint32_t kFrontOnly = 3;

        template<typename Mesh, typename Index> ModelPart part_of(const Mesh& mesh, Index&& index)
        {
            ModelPart part;
            part.blend = blend_of(mesh.transparency);
            part.one_sided = mesh.sides == kFrontOnly;
            part.vertices.reserve(mesh.vertex_count);
            for (std::size_t i = 0; i < mesh.vertex_count; ++i)
            {
                const auto v = mesh.vertex(i);
                part.vertices.push_back({ { v.position[0], v.position[1], v.position[2] }, { v.normal[0], v.normal[1], v.normal[2] }, { v.texcoord[0], v.texcoord[1] } });
                if constexpr (requires { v.weights; })
                {
                    part.vertices.back().bones = v.bones;
                    part.vertices.back().weights = v.weights;
                }
            }
            part.indices.reserve(mesh.index_count);
            for (std::size_t i = 0; i + 2 < mesh.index_count; i += 3)
            {
                const std::uint32_t a = index(mesh, i);
                const std::uint32_t b = index(mesh, i + 1);
                const std::uint32_t c = index(mesh, i + 2);
                if (a >= mesh.vertex_count || b >= mesh.vertex_count || c >= mesh.vertex_count)
                    continue;
                part.indices.insert(part.indices.end(), { a, b, c });
            }
            return part;
        }

        std::string tagged(std::string_view name, std::string_view tag)
        {
            return std::string(name) + std::string(tag);
        }

    } // namespace

    struct ModelLoader::Files
    {
        const AssetIndex& index;
        std::map<std::string, std::unique_ptr<OpenFile>, std::less<>> open;

        OpenFile* get(std::string_view path)
        {
            auto it = open.find(path);
            if (it == open.end())
            {
                auto overlay = ovl::Overlay::load(index.file(path));
                std::unique_ptr<OpenFile> file;
                if (overlay)
                    file = std::make_unique<OpenFile>(OpenFile { std::move(*overlay), { }, { }, { }, { }, { }, { }, { }, { }, { }, { } });
                it = open.emplace(std::string(path), std::move(file)).first;
            }
            return it->second.get();
        }

        /// The file holding `symbol` nearest to `near`, with its path. Nothing when none does.
        std::optional<std::pair<std::string, OpenFile*>> holding(std::string_view symbol, std::string_view near)
        {
            const auto path = index.find(symbol, near);
            if (!path)
                return std::nullopt;
            OpenFile* file = get(*path);
            if (file == nullptr)
                return std::nullopt;
            return std::pair { *path, file };
        }
    };

    namespace
    {

        template<typename T> std::vector<std::pair<std::string_view, std::optional<T>>> pairs(auto&& items, auto member)
        {
            std::vector<std::pair<std::string_view, std::optional<T>>> out;
            out.reserve(items.size());
            for (auto& item : items)
                out.emplace_back(item.name, std::move(item.*member));
            return out;
        }

        const ovl::SceneryItem* item_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.items, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::SceneryItem>(ovl::read_scenery_items(o, p), &ovl::SceneryItemEntry::item); }), name);
        }
        const ovl::SceneryVisual* visual_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.visuals, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::SceneryVisual>(ovl::read_scenery_visuals(o, p), &ovl::SceneryVisualItem::visual); }), name);
        }
        const ovl::StaticShape* static_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.statics, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::StaticShape>(ovl::read_static_shapes(o, p), &ovl::StaticShapeItem::shape); }), name);
        }
        const ovl::BoneShape* bone_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.bones, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::BoneShape>(ovl::read_bone_shapes(o, p), &ovl::BoneShapeItem::shape); }), name);
        }
        const ovl::BoneAnim* anim_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.anims, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::BoneAnim>(ovl::read_bone_anims(o, p), &ovl::BoneAnimItem::anim); }), name);
        }

        constexpr std::string_view kSeat = "peep";     ///< A bone so named and numbered is where a rider sits
        constexpr std::string_view kView = "camera";   ///< And so a fixed camera on a ride
        constexpr std::string_view kVendor = "vendor"; ///< A point or bone so named is where a seller stands

        /// Whether `bone` is named `kind` and a number.
        bool is_numbered(const ovl::Bone& b, std::string_view kind)
        {
            return b.name.size() > kind.size() && strings::equals_ignoring_case(b.name.substr(0, kind.size()), kind) && std::ranges::all_of(b.name.substr(kind.size()), [](char c) { return c >= '0' && c <= '9'; });
        }

        /// The bones named `kind` and a number, by their place in the shape, in number order.
        std::vector<std::int32_t> numbered_bones(const ovl::BoneShape& shape, std::string_view kind)
        {
            std::vector<std::pair<int, std::int32_t>> found;
            for (std::size_t b = 0; b < shape.bones.size(); ++b)
                if (is_numbered(shape.bones[b], kind))
                    found.emplace_back(std::stoi(std::string(shape.bones[b].name.substr(kind.size()))), static_cast<std::int32_t>(b));
            std::ranges::sort(found);
            std::vector<std::int32_t> out;
            for (const auto& f : found)
                out.push_back(f.second);
            return out;
        }

        /// The places of the bones named `kind` and a number, model space, in number order.
        std::vector<glm::mat4> numbered_places(const ovl::BoneShape& shape, std::string_view kind)
        {
            std::vector<glm::mat4> out;
            for (const std::int32_t b : numbered_bones(shape, kind))
            {
                // Row vectors in rows read as glm's columns: the same transform, for column vectors.
                glm::mat4 place { 1.0f };
                for (glm::length_t c = 0; c < 4; ++c)
                    for (glm::length_t r = 0; r < 4; ++r)
                        place[c][r] = shape.bones[static_cast<std::size_t>(b)].world[static_cast<std::size_t>(c * 4 + r)];
                out.push_back(place);
            }
            return out;
        }

        glm::mat4 column_matrix(const std::array<float, 16>& rows)
        {
            // Row vectors in rows read as glm's columns: the same transform, for column vectors.
            glm::mat4 m { 1.0f };
            for (glm::length_t c = 0; c < 4; ++c)
                for (glm::length_t r = 0; r < 4; ++r)
                    m[c][r] = rows[static_cast<std::size_t>(c * 4 + r)];
            return m;
        }

        /// The rig of `shape`, without motions.
        BoneRig rig_of(const ovl::BoneShape& shape)
        {
            BoneRig rig;
            for (const ovl::Bone& b : shape.bones)
            {
                rig.parents.push_back(b.parent);
                rig.rest_local.push_back(column_matrix(b.local));
                rig.rest_world.push_back(column_matrix(b.world));
            }
            return rig;
        }

        /// How `anim` moves the bones of `shape`, matched by name.
        BoneMotion motion_of(const ovl::BoneShape& shape, const ovl::BoneAnim& anim)
        {
            BoneMotion m;
            m.seconds = anim.duration;
            m.positions.resize(shape.bones.size());
            m.rotations.resize(shape.bones.size());
            const auto keys = [](std::span<const ovl::BoneKeyframe> frames) {
                BoneKeys k;
                for (const ovl::BoneKeyframe& f : frames)
                {
                    k.times.push_back(f.time);
                    k.values.emplace_back(f.value[0], f.value[1], f.value[2]);
                }
                return k;
            };
            for (const ovl::BoneTrack& track : anim.bones)
            {
                const auto bone = std::ranges::find(shape.bones, track.name, &ovl::Bone::name);
                if (bone == shape.bones.end())
                    continue;
                const auto b = static_cast<std::size_t>(bone - shape.bones.begin());
                m.positions[b] = keys(track.positions);
                m.rotations[b] = keys(track.rotations);
            }
            return m;
        }

        /// `mesh` as one part per bone, each part taking the triangles whose first corner that bone carries most.
        /// shortcut: a corner shared by bones follows the one with most weight, upgrade if an arm's joints show
        /// seams.
        std::vector<ModelPart> parts_by_bone(ModelPart whole)
        {
            std::vector<std::int32_t> carrier(whole.vertices.size(), 0);
            for (std::size_t i = 0; i < carrier.size(); ++i)
            {
                const ModelVertex& v = whole.vertices[i];
                carrier[i] = v.bones[static_cast<std::size_t>(std::ranges::max_element(v.weights) - v.weights.begin())];
            }
            std::map<std::int32_t, std::pair<ModelPart, std::vector<std::uint32_t>>> by_bone;
            for (std::size_t t = 0; t + 2 < whole.indices.size(); t += 3)
            {
                const std::int32_t bone = carrier[whole.indices[t]];
                auto [at, added] = by_bone.try_emplace(bone);
                auto& [part, remap] = at->second;
                if (added)
                {
                    part.texture = whole.texture;
                    part.slots = whole.slots;
                    part.blend = whole.blend;
                    part.board = whole.board;
                    part.bone = bone;
                    remap.assign(whole.vertices.size(), UINT32_MAX);
                }
                for (std::size_t k = t; k < t + 3; ++k)
                {
                    std::uint32_t& to = remap[whole.indices[k]];
                    if (to == UINT32_MAX)
                    {
                        to = static_cast<std::uint32_t>(part.vertices.size());
                        part.vertices.push_back(whole.vertices[whole.indices[k]]);
                    }
                    part.indices.push_back(to);
                }
            }
            std::vector<ModelPart> out;
            for (auto& [_, entry] : by_bone)
                out.push_back(std::move(entry.first));
            return out;
        }

        const ovl::FlexiTexture* texture_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.textures, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::FlexiTexture>(ovl::read_flexi_textures(o, p), &ovl::FlexiTextureItem::texture); }), name);
        }
        const ovl::RideTrain* train_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.trains, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::RideTrain>(ovl::read_ride_trains(o, p), &ovl::RideTrainItem::train); }), name);
        }
        const ovl::RideCar* car_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.cars, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::RideCar>(ovl::read_ride_cars(o, p), &ovl::RideCarItem::car); }), name);
        }

        const ovl::TrackSection* section_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.sections, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::TrackSection>(ovl::read_track_sections(o, p), &ovl::TrackSectionItem::section); }), name);
        }
        const ovl::Spline* spline_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.splines, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::Spline>(ovl::read_splines(o, p), &ovl::SplineItem::spline); }), name);
        }

        /// The walkways of a flat ride in `f`: the spline whose name ends in "loop", and those ending in "path"
        /// and a number, in number order, each followed curve by curve.
        Walkways walkways_in(OpenFile& f)
        {
            const auto points = [](const ovl::Spline& spline) {
                constexpr int kSteps = 4; // Points a curve
                std::vector<glm::vec3> out;
                for (std::size_t s = 0; s + 1 < spline.nodes.size(); ++s)
                    for (int k = 0; k < kSteps; ++k)
                    {
                        const auto p = ovl::spline_point(spline, s, static_cast<float>(k) / kSteps);
                        out.emplace_back(p[0], p[1], p[2]);
                    }
                if (!spline.nodes.empty())
                    out.emplace_back(spline.nodes.back().position[0], spline.nodes.back().position[1], spline.nodes.back().position[2]);
                return out;
            };
            const auto ends_with = [](std::string_view name, std::string_view end) { return name.size() >= end.size() && strings::equals_ignoring_case(name.substr(name.size() - end.size()), end); };
            Walkways ways;
            std::vector<std::pair<int, std::vector<glm::vec3>>> paths;
            for (const auto& [name, spline] : entries(f.splines, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::Spline>(ovl::read_splines(o, p), &ovl::SplineItem::spline); }))
            {
                const std::size_t digits = name.size() - std::min(name.size(), name.find_last_not_of("0123456789") + 1);
                const std::string_view stem = name.substr(0, name.size() - digits);
                if (ends_with(name, "loop"))
                    ways.loop = points(spline);
                else if (digits > 0 && ends_with(stem, "path") && !ends_with(stem, "mechpath"))
                    paths.emplace_back(std::stoi(std::string(name.substr(stem.size()))), points(spline));
            }
            std::ranges::sort(paths, { }, &std::pair<int, std::vector<glm::vec3>>::first);
            for (auto& path : paths)
                ways.paths.push_back(std::move(path.second));
            return ways;
        }

    } // namespace

    ModelLoader::ModelLoader(const AssetIndex& index)
        : index_(index)
        , files_(std::make_unique<Files>(Files { index, { } }))
    {
    }

    ModelLoader::~ModelLoader() = default;

    std::string ModelLoader::texture_key(std::string_view name, std::string_view near)
    {
        if (name.empty())
            return { };
        const auto found = files_->holding(tagged(name, kTextureTag), near);
        if (!found)
            return { };
        std::string key = std::format("{}:{}", found->first, name);
        if (tried_pictures_.contains(key))
            return pictures_.contains(key) ? key : std::string();
        tried_pictures_.emplace(key, true);
        const ovl::FlexiTexture* texture = texture_in(*found->second, name);
        if (texture == nullptr || texture->frames.empty())
            return { };
        const std::size_t frame = texture->sequence.empty() ? 0 : texture->sequence.front();
        if (frame >= texture->frames.size())
            return { };
        auto picture = palette_image(texture->frames[frame], texture->flags);
        if (!picture)
            return { };
        pictures_.emplace(key, std::move(*picture));
        return key;
    }

    PaletteImage water_picture()
    {
        // The lakes' colour, its ripple crests a little lighter, in a soft criss-cross.
        // shortcut: a still picture, moving water would need its own shader; upgrade when fountains get one.
        constexpr std::uint32_t kSize = 64;
        constexpr int kShades = 16;
        PaletteImage picture;
        picture.width = kSize;
        picture.height = kSize;
        constexpr float kCrest = 0.25f; ///< How much lighter the crests are
        for (int i = 0; i < kShades; ++i)
        {
            const glm::vec3 shade = glm::mix(glm::vec3(kWaterColour), glm::vec3(1.0f), kCrest * static_cast<float>(i) / (kShades - 1));
            picture.palette[i] = { static_cast<std::uint8_t>(shade.r * 255), static_cast<std::uint8_t>(shade.g * 255), static_cast<std::uint8_t>(shade.b * 255) };
        }
        const float alpha = kWaterColour.a * 255;
        picture.indices.resize(kSize * kSize);
        picture.alpha.resize(kSize * kSize);
        constexpr float kTurn = 6.2831853f / kSize;
        for (std::uint32_t y = 0; y < kSize; ++y)
        {
            for (std::uint32_t x = 0; x < kSize; ++x)
            {
                // Whole periods across the picture, so it tiles.
                const float wave = (std::sin(kTurn * 2 * x + std::sin(kTurn * 3 * y)) + std::sin(kTurn * 3 * y + std::sin(kTurn * 2 * x))) * 0.25f + 0.5f;
                const std::size_t at = y * kSize + x;
                picture.indices[at] = static_cast<std::uint8_t>(std::clamp(wave * kShades, 0.0f, kShades - 1.0f));
                picture.alpha[at] = static_cast<std::uint8_t>(std::clamp(alpha - 20 + wave * 40, 1.0f, 254.0f));
            }
        }
        return picture;
    }

    void ModelLoader::set_texture(ModelPart& part, std::string_view name, std::string_view style, std::string_view near)
    {
        if (strings::equals_ignoring_case(style, "SIWater"))
        {
            if (!pictures_.contains(kWaterPicture))
                pictures_.emplace(std::string(kWaterPicture), water_picture());
            part.texture = std::string(kWaterPicture);
            part.slots = 0;
            for (ModelVertex& v : part.vertices)
                v.position.y += kWaterLift;
            return;
        }
        for (const auto& [placeholder, key] : { std::pair { kGroundSurfaceTexture, kGroundSurfacePicture }, std::pair { kGroundCliffTexture, kGroundCliffPicture } })
        {
            if (strings::equals_ignoring_case(name, placeholder))
            {
                part.texture = std::string(key);
                part.slots = 0;
                return;
            }
        }
        part.texture = texture_key(name, near);
        const auto picture = pictures_.find(part.texture);
        part.slots = picture != pictures_.end() ? picture->second.slots : 0;
    }

    std::uint32_t colour_slots(const ObjectModel& model)
    {
        std::uint32_t slots = 0;
        for (const ModelLevel& level : model.levels)
        {
            for (const ModelPart& part : level.parts)
                slots |= part.slots;
            if (level.billboard)
                slots |= level.billboard->slots;
        }
        return slots;
    }

    float level_reach(const ObjectModel& model, std::size_t level, LevelScale scale)
    {
        const float last = model.levels.back().distance * scale.distance;
        return level + 1 < model.levels.size() ? std::min(model.levels[level].distance * scale.detail, last) : last;
    }

    std::optional<std::size_t> level_at(const ObjectModel& model, float distance, LevelScale scale)
    {
        for (std::size_t i = 0; i < model.levels.size(); ++i)
        {
            if (distance <= level_reach(model, i, scale))
                return i;
        }
        return std::nullopt;
    }

    std::optional<ObjectModel> ModelLoader::load(std::string_view symbol, std::string_view overlay, bool modelled_plants)
    {
        const std::string hint = index_.overlay(overlay).value_or(std::string(overlay));
        const auto item_file = files_->holding(tagged(symbol, kItemTag), hint);
        if (!item_file)
        {
            // A look of its own, with no item to place it as, such as an animal's crate: drawn as it is.
            const auto visual_file = files_->holding(tagged(symbol, kVisualTag), hint);
            const ovl::SceneryVisual* visual = visual_file ? visual_in(*visual_file->second, symbol) : nullptr;
            if (visual == nullptr)
                return std::nullopt;
            ObjectModel model;
            add_levels(model, *visual, visual_file->first);
            return model.levels.empty() ? std::nullopt : std::optional(std::move(model));
        }
        const ovl::SceneryItem* item = item_in(*item_file->second, symbol);
        if (item == nullptr)
            return std::nullopt;
        const std::string near = item_file->first;

        // The visuals that can be read, with the file each is in; a broken look apart.
        constexpr std::string_view kBroken = "_Broken";
        std::vector<std::pair<const ovl::SceneryVisual*, std::string>> visuals;
        std::optional<std::pair<const ovl::SceneryVisual*, std::string>> broken;
        for (const std::string_view visual_name : item->visuals)
        {
            const auto visual_file = files_->holding(tagged(visual_name, kVisualTag), near);
            if (!visual_file)
                continue;
            if (const ovl::SceneryVisual* visual = visual_in(*visual_file->second, visual_name))
            {
                if (visual_name.size() > kBroken.size() && strings::equals_ignoring_case(visual_name.substr(visual_name.size() - kBroken.size()), kBroken))
                    broken.emplace(visual, visual_file->first);
                else
                    visuals.emplace_back(visual, visual_file->first);
            }
        }
        if (visuals.empty())
            return std::nullopt;
        // Only plants list a modelled look after their flat one; other items' later visuals are something else.
        const bool plant = item->type <= std::to_underlying(ItemCategory::Flowers);
        const auto& [visual, visual_path] = modelled_plants && plant ? visuals.back() : visuals.front();

        ObjectModel model;
        model.positioning = item->positioning;
        model.category = item->type;
        model.squares = { static_cast<std::int32_t>(std::max(item->x_squares, 1u)), static_cast<std::int32_t>(std::max(item->z_squares, 1u)) };
        model.sway = visual->sway;
        model.support_group = item->group;
        if (!item->squares.empty())
            model.lowest_level = std::ranges::min(item->squares, { }, &ovl::SceneryItemSquare::min_height).min_height;
        model.supports = support_footprint(*item);
        if (item->squares.size() == static_cast<std::size_t>(model.squares[0]) * static_cast<std::size_t>(model.squares[1]))
        {
            for (const ovl::SceneryItemSquare& s : item->squares)
                model.solid.push_back((s.flags & kSquareSolid) != 0 ? std::optional(std::array { s.min_height, s.max_height }) : std::nullopt);
        }
        model.water_only = (item->flags & ovl::kItemWaterOnly) != 0;
        for (std::size_t i = 0; i < model.default_colours.size(); ++i)
            model.default_colours[i] = static_cast<std::int32_t>(item->default_colours[i]);
        model.photo_spot = std::ranges::contains(item->parameters, std::string_view { "VistaPoint" }, &ovl::SceneryItemParameter::name);
        // A setting's words, such as " EnableNoEntry QueueSign".
        model.no_entry = std::ranges::any_of(item->parameters, [](const ovl::SceneryItemParameter& p) { return std::ranges::any_of(std::views::split(p.value, ' '), [](const auto word) { return strings::equals_ignoring_case(std::string_view(word), "EnableNoEntry"); }); });
        model.ground_height = (item->flags & ovl::kItemOnGroundHeight) != 0;
        // Buildings, rides and path pieces carry the bit too but stay rigid; only plants bend.
        model.fits_ground = (item->flags & ovl::kItemFitsGround) != 0 && item->type <= std::to_underlying(ItemCategory::Flowers);
        if ((item->flags & ovl::kItemFenceOutline) != 0 && item->squares.size() == static_cast<std::size_t>(model.squares[0]) * static_cast<std::size_t>(model.squares[1]))
        {
            // Inside are the solid squares that reach down to the ground.
            for (const ovl::SceneryItemSquare& square : item->squares)
                model.fence_outline.push_back((square.flags & ovl::kSquareSolid) != 0 && square.min_height <= 0);
        }
        add_levels(model, *visual, visual_path);
        if (model.levels.empty())
            return std::nullopt;
        if (broken)
        {
            ObjectModel worn;
            add_levels(worn, *broken->first, broken->second);
            if (worn.levels.size() == model.levels.size())
                model.broken_levels = std::move(worn.levels);
        }
        model.price = item->cost;
        model.removal_cost = item->removal_cost;
        add_flow(model, symbol, near);
        return model;
    }

    void ModelLoader::add_levels(ObjectModel& model, const ovl::SceneryVisual& visual, std::string_view visual_path)
    {
        for (const ovl::SceneryVisualLod& lod : visual.lods)
        {
            ModelLevel level;
            level.distance = lod.distance;
            if (lod.mesh_type == static_cast<std::uint32_t>(ovl::LodMeshType::StaticShape))
            {
                const ovl::StaticShape* shape = add_static(level, lod.static_shape, visual_path);
                if (shape == nullptr)
                    continue;
                if (const auto vendor = std::ranges::find_if(shape->effects, [](const ovl::ShapeEffect& e) { return strings::equals_ignoring_case(e.name, kVendor); }); !model.vendor_place && vendor != shape->effects.end())
                    model.vendor_place = column_matrix(vendor->transform);
                if (model.lights.empty() || model.effects.empty())
                {
                    std::vector<NamedPlace> places;
                    for (const ovl::ShapeEffect& e : shape->effects)
                        places.push_back({ .name = e.name, .place = column_matrix(e.transform) });
                    if (model.lights.empty())
                        model.lights = model_lights(places);
                    if (model.effects.empty())
                        model.effects = model_effects(places);
                }
            }
            else if (lod.mesh_type == static_cast<std::uint32_t>(ovl::LodMeshType::BoneShape))
            {
                const auto file = files_->holding(tagged(lod.bone_shape, kBoneTag), visual_path);
                const ovl::BoneShape* shape = file ? bone_in(*file->second, lod.bone_shape) : nullptr;
                if (shape == nullptr)
                    continue;
                model.seats = std::max(model.seats, static_cast<std::uint32_t>(numbered_bones(*shape, kSeat).size()));
                if (model.walkways.loop.empty() && model.seats > 0)
                    model.walkways = walkways_in(*file->second);
                if (model.seat_places.empty())
                    model.seat_places = numbered_places(*shape, kSeat);
                if (model.view_places.empty())
                    model.view_places = numbered_places(*shape, kView);
                if (const auto vendor = std::ranges::find_if(shape->bones, [](const ovl::Bone& b) { return strings::equals_ignoring_case(b.name, kVendor); }); !model.vendor_place && vendor != shape->bones.end())
                    model.vendor_place = column_matrix(vendor->world);
                // A car on an arm: each bone is carried by the nearest of the arm's joints it hangs from.
                constexpr std::array<std::string_view, 4> kArmJoints { "ik_robotjoint01", "ik_robotjoint02", "ik_robotjoint03", "ik_robotjoint04" };
                std::array<std::int32_t, 4> joint_bones;
                joint_bones.fill(-1);
                for (std::size_t b = 0; b < shape->bones.size(); ++b)
                    for (std::size_t j = 0; j < kArmJoints.size(); ++j)
                        if (strings::equals_ignoring_case(shape->bones[b].name, kArmJoints[j]))
                            joint_bones[j] = static_cast<std::int32_t>(b);
                std::vector<std::int32_t> joint_of;
                if (!std::ranges::contains(joint_bones, -1))
                {
                    joint_of.assign(shape->bones.size(), -1);
                    for (std::size_t b = 0; b < shape->bones.size(); ++b)
                    {
                        auto up = static_cast<std::int32_t>(b);
                        for (std::size_t hops = 0; up >= 0 && up < std::ssize(shape->bones) && hops < shape->bones.size() && joint_of[b] < 0; ++hops, up = shape->bones[static_cast<std::size_t>(up)].parent)
                            if (const auto j = std::ranges::find(joint_bones, up); j != joint_bones.end())
                                joint_of[b] = static_cast<std::int32_t>(j - joint_bones.begin());
                    }
                    if (model.arm_joints.empty())
                        for (const std::int32_t b : joint_bones)
                            model.arm_joints.push_back(column_matrix(shape->bones[static_cast<std::size_t>(b)].world));
                }
                const bool rigged = joint_of.empty() && !model.rig && !lod.animations.empty();
                if (model.lights.empty() || model.effects.empty())
                {
                    std::vector<NamedPlace> places;
                    for (std::size_t b = 0; b < shape->bones.size(); ++b)
                    {
                        const auto bone = static_cast<std::int32_t>(b);
                        places.push_back({ .name = shape->bones[b].name, .place = column_matrix(shape->bones[b].world), .bone = rigged ? bone : !joint_of.empty() ? joint_of[b] : -1 });
                    }
                    if (model.lights.empty())
                        model.lights = model_lights(places);
                    if (model.effects.empty())
                        model.effects = model_effects(places);
                }
                if (rigged)
                {
                    model.rig = rig_of(*shape);
                    model.rig_level = model.levels.size();
                    // In number order, as the seats' and cameras' places are.
                    model.seat_bones = numbered_bones(*shape, kSeat);
                    model.view_bones = numbered_bones(*shape, kView);
                }
                for (const std::string_view name : lod.animations)
                {
                    const auto anim_file = files_->holding(tagged(name, kAnimTag), visual_path);
                    const ovl::BoneAnim* anim = anim_file ? anim_in(*anim_file->second, name) : nullptr;
                    if (rigged)
                        model.rig->motions.push_back(anim != nullptr ? motion_of(*shape, *anim) : BoneMotion { });
                    if (model.animation_seconds.size() < lod.animations.size())
                    {
                        model.animation_seconds.push_back(anim != nullptr ? anim->duration : 0.0f);
                        model.animation_names.emplace_back(name);
                    }
                }
                for (const ovl::BoneMesh& mesh : shape->meshes)
                {
                    ModelPart part = part_of(mesh, [](const ovl::BoneMesh& m, std::size_t i) { return static_cast<std::uint32_t>(m.index(i)); });
                    set_texture(part, mesh.texture, mesh.style, file->first);
                    part.board = (mesh.texture_flags & ovl::kMeshFlagBoard) != 0;
                    if (!joint_of.empty())
                        for (ModelPart& one : parts_by_bone(std::move(part)))
                        {
                            one.bone = one.bone >= 0 && one.bone < std::ssize(joint_of) ? joint_of[static_cast<std::size_t>(one.bone)] : -1;
                            level.parts.push_back(std::move(one));
                        }
                    else
                        level.parts.push_back(std::move(part));
                }
            }
            else if (lod.mesh_type == static_cast<std::uint32_t>(ovl::LodMeshType::Billboard))
            {
                if (lod.width <= 0 || lod.height <= 0)
                    continue;
                Billboard board;
                board.width = lod.width;
                board.height = lod.height;
                // The level gives u0, u1, v0, v1 with v0 at the foot of the picture.
                board.uv_min = { lod.texcoords[0], lod.texcoords[3] };
                board.uv_max = { lod.texcoords[1], lod.texcoords[2] };
                board.texture = texture_key(lod.texture, visual_path);
                const auto picture = pictures_.find(board.texture);
                board.slots = picture != pictures_.end() ? picture->second.slots : 0;
                level.billboard = std::move(board);
            }
            else
            {
                continue;
            }
            model.levels.push_back(std::move(level));
        }
    }

    void ModelLoader::add_flow(ObjectModel& model, std::string_view symbol, std::string_view path)
    {
        OpenFile* file = files_->get(path);
        // shortcut: finds the piece's track section by the item's name, which nearly all sections share; upgrade
        // if a slide shows pieces left dry.
        const ovl::TrackSection* section = file != nullptr ? section_in(*file, symbol) : nullptr;
        // Only lift pieces move the parts marked as chain; on other pieces those stay still.
        // shortcut: marked parts of pieces that are no lift stay still; upgrade if such a piece is seen moving its
        // picture in the game.
        const bool lift = section != nullptr && (section->flags[0] & kPieceChain) != 0;
        for (ModelLevel& level : model.levels)
            for (ModelPart& part : level.parts)
                part.chain = part.chain && lift;
        if (section == nullptr || section->water_splines[0].empty() || section->water_splines[1].empty())
            return;
        const ovl::Spline* left = spline_in(*file, section->water_splines[0]);
        const ovl::Spline* right = spline_in(*file, section->water_splines[1]);
        if (left == nullptr || right == nullptr)
            return;
        const auto strip = flow_strip(*left, *right, std::string(kFlowPicture));
        if (!strip)
            return;
        for (ModelLevel& level : model.levels)
            level.parts.push_back(*strip);
    }

    const ovl::StaticShape* ModelLoader::add_static(ModelLevel& level, std::string_view name, std::string_view near)
    {
        const auto file = files_->holding(tagged(name, kStaticTag), near);
        const ovl::StaticShape* shape = file ? static_in(*file->second, name) : nullptr;
        if (shape == nullptr)
            return nullptr;
        for (const ovl::ShapeMesh& mesh : shape->meshes)
        {
            level.parts.push_back(part_of(mesh, [](const ovl::ShapeMesh& m, std::size_t i) { return m.index(i); }));
            set_texture(level.parts.back(), mesh.texture, mesh.style, file->first);
            level.parts.back().board = (mesh.texture_flags & ovl::kMeshFlagBoard) != 0;
            level.parts.back().chain = (mesh.texture_flags & ovl::kMeshFlagChain) != 0;
        }
        return shape;
    }

    std::optional<ObjectModel> ModelLoader::load_shape(std::string_view shape, std::string_view overlay)
    {
        ModelLevel level;
        if (!add_static(level, shape, index_.overlay(overlay).value_or(std::string(overlay))))
            return std::nullopt;
        ObjectModel model;
        model.levels.push_back(std::move(level));
        return model;
    }

    std::optional<ObjectModel> ModelLoader::load_car(std::string_view train, std::string_view overlay, std::size_t slot)
    {
        const std::string hint = index_.overlay(overlay).value_or(std::string(overlay));
        const auto train_file = files_->holding(tagged(train, kTrainTag), hint);
        const ovl::RideTrain* found = train_file ? train_in(*train_file->second, train) : nullptr;
        if (found == nullptr || slot >= found->cars.size() || found->cars[slot].empty())
            return std::nullopt;
        const auto car_file = files_->holding(tagged(found->cars[slot], kCarTag), train_file->first);
        const ovl::RideCar* car = car_file ? car_in(*car_file->second, found->cars[slot]) : nullptr;
        if (car == nullptr)
            return std::nullopt;
        const auto visual_file = files_->holding(tagged(car->visual, kVisualTag), car_file->first);
        const ovl::SceneryVisual* visual = visual_file ? visual_in(*visual_file->second, car->visual) : nullptr;
        if (visual == nullptr)
            return std::nullopt;
        ObjectModel model;
        add_levels(model, *visual, visual_file->first);
        // The car's other pieces, each drawn at a place on it: `name`'s levels added to the car's, finest to
        // finest, as many as the car has.
        const auto add = [&](std::string_view name, const glm::mat4& place) -> std::optional<ObjectModel> {
            const auto file = name.empty() ? std::nullopt : files_->holding(tagged(name, kVisualTag), car_file->first);
            const ovl::SceneryVisual* piece = file ? visual_in(*file->second, name) : nullptr;
            if (piece == nullptr)
                return std::nullopt;
            ObjectModel pieces;
            add_levels(pieces, *piece, file->first);
            for (ModelLight light : pieces.lights)
            {
                light.position = glm::vec3(place * glm::vec4(light.position, 1.0f));
                light.bone = -1;
                model.lights.push_back(light);
            }
            if (model.levels.empty())
                for (const ModelLevel& level : pieces.levels)
                    model.levels.push_back({ .distance = level.distance, .parts = { }, .billboard = std::nullopt });
            const glm::mat3 turn { place };
            for (std::size_t i = 0; i < model.levels.size() && !pieces.levels.empty(); ++i)
                for (ModelPart one : pieces.levels[std::min(i, pieces.levels.size() - 1)].parts)
                {
                    for (ModelVertex& vertex : one.vertices)
                    {
                        vertex.position = glm::vec3(place * glm::vec4(vertex.position, 1.0f));
                        vertex.normal = glm::normalize(turn * vertex.normal);
                    }
                    one.bone = -1; // Cars are drawn unrigged
                    model.levels[i].parts.push_back(std::move(one));
                }
            return pieces;
        };
        // Where a piece's finest shape has the bone `bone`, in its own space.
        const auto bone_of = [&](std::string_view name, std::string_view bone) -> std::optional<glm::mat4> {
            const auto file = name.empty() ? std::nullopt : files_->holding(tagged(name, kVisualTag), car_file->first);
            const ovl::SceneryVisual* piece = file ? visual_in(*file->second, name) : nullptr;
            if (piece == nullptr || piece->lods.empty() || piece->lods.front().mesh_type != static_cast<std::uint32_t>(ovl::LodMeshType::BoneShape))
                return std::nullopt;
            const auto shape_file = files_->holding(tagged(piece->lods.front().bone_shape, kBoneTag), file->first);
            if (const ovl::BoneShape* shape = shape_file ? bone_in(*shape_file->second, piece->lods.front().bone_shape) : nullptr)
                for (const ovl::Bone& b : shape->bones)
                    if (strings::equals_ignoring_case(b.name, bone))
                        return column_matrix(b.world);
            return std::nullopt;
        };
        const glm::mat4 rest { 1.0f };

        // The part that spins or swings on the car, at rest: its pivot bone on the car's, each piece's origin
        // standing in for a pivot it lacks. Riders sit in it where it has the seats.
        const glm::vec3 pivot { bone_of(car->visual, "pivot").value_or(rest)[3] };
        const glm::vec3 part_pivot { bone_of(car->part, "pivot").value_or(rest)[3] };
        const glm::mat4 part_place = glm::translate(rest, pivot - part_pivot);
        if (const auto part = add(car->part, part_place))
        {
            if (model.seat_places.empty())
                for (const glm::mat4& seat : part->seat_places)
                    model.seat_places.push_back(part_place * seat);
            model.seats = std::max(model.seats, part->seats);
        }

        // Axles on the car's axle bones, and wheels on its wheel bones or else on their axle's ends. A piece
        // with no place to go is left off.
        constexpr std::array<std::string_view, 2> kAxleBones { "axelf", "axelr" };
        constexpr std::array<std::string_view, 4> kWheelBones { "wheelfr", "wheelfl", "wheelrr", "wheelrl" };
        constexpr std::array<std::string_view, 2> kAxleEnds { "wheelr", "wheell" };
        std::array<std::optional<glm::mat4>, 2> axles;
        for (std::size_t i = 0; i < axles.size(); ++i)
            if (!car->axles[i].empty())
                if ((axles[i] = bone_of(car->visual, kAxleBones[i])))
                    add(car->axles[i], *axles[i]);
        for (std::size_t i = 0; i < car->wheels.size(); ++i)
        {
            std::optional<glm::mat4> place = car->wheels[i].empty() ? std::nullopt : bone_of(car->visual, kWheelBones[i]);
            if (!place && axles[i / 2])
                if (const auto end = bone_of(car->axles[i / 2], kAxleEnds[i % 2]))
                    place = *axles[i / 2] * *end;
            if (place)
                add(car->wheels[i], *place);
        }
        if (const auto front = bone_of(car->visual, "carfront"), rear = bone_of(car->visual, "carrear"); front && rear)
        {
            CarFit& fit = model.car_fit.emplace();
            const float front_z = (*front)[3].z;
            fit.length = front_z - (*rear)[3].z;
            // By axle, the middle of the wheels it has bones for, on the car or at its axle's ends.
            std::array<glm::vec3, 2> sum { };
            std::array<int, 2> found { };
            for (std::size_t i = 0; i < kWheelBones.size(); ++i)
            {
                std::optional<glm::mat4> wheel = bone_of(car->visual, kWheelBones[i]);
                if (!wheel && axles[i / 2])
                    if (const auto end = bone_of(car->axles[i / 2], kAxleEnds[i % 2]))
                        wheel = *axles[i / 2] * *end;
                if (wheel)
                {
                    sum[i / 2] += glm::vec3((*wheel)[3]);
                    ++found[i / 2];
                }
            }
            const glm::vec3 middle { 0, 0, front_z - fit.length / 2 };
            const glm::vec3 front_wheels = found[0] > 0 ? sum[0] / static_cast<float>(found[0]) : middle;
            const glm::vec3 rear_wheels = found[1] > 0 ? sum[1] / static_cast<float>(found[1]) : middle;
            fit.front_wheels = front_z - front_wheels.z;
            fit.rear_wheels = front_z - rear_wheels.z;
            fit.on_track = (front_wheels + rear_wheels) / 2.0f;
        }
        const std::optional<glm::mat4> front = bone_of(car->visual, "camerafront");
        model.ride_eyes = { bone_of(car->visual, "nosecamera").or_else([&] { return front; }), front, bone_of(car->visual, "camerarear") };
        model.ride_look = found->camera;
        if (model.levels.empty())
            return std::nullopt;
        return model;
    }

} // namespace rct3::park
