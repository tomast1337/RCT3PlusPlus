// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_models.hpp"

#include "ovl/bone_shape.hpp"
#include "ovl/flexi_texture.hpp"
#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/scenery_visual.hpp"
#include "ovl/static_shape.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <utility>

namespace rct3::park
{

    namespace
    {

        constexpr std::string_view kItemTag = ":sid";
        constexpr std::string_view kVisualTag = ":svd";
        constexpr std::string_view kStaticTag = ":shs";
        constexpr std::string_view kBoneTag = ":bsh";
        constexpr std::string_view kTextureTag = ":ftx";

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
            std::optional<ByName<ovl::FlexiTexture>> textures;
        };

        /// Every readable entry of both parts, read the first time it is asked for.
        template<typename T, typename Read> const ByName<T>& entries(std::optional<ByName<T>>& cache, const ovl::Overlay& overlay, Read&& read)
        {
            if (!cache)
            {
                cache.emplace();
                for (const ovl::Part* part : { &overlay.common(), overlay.unique() })
                {
                    if (part == nullptr)
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

        template<typename Mesh, typename Index> ModelPart part_of(const Mesh& mesh, Index&& index)
        {
            ModelPart part;
            part.blend = blend_of(mesh.transparency);
            part.vertices.reserve(mesh.vertex_count);
            for (std::size_t i = 0; i < mesh.vertex_count; ++i)
            {
                const auto v = mesh.vertex(i);
                part.vertices.push_back({ { v.position[0], v.position[1], v.position[2] }, { v.normal[0], v.normal[1], v.normal[2] }, { v.texcoord[0], v.texcoord[1] } });
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
                    file = std::make_unique<OpenFile>(OpenFile { std::move(*overlay), { }, { }, { }, { }, { } });
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
        const ovl::FlexiTexture* texture_in(OpenFile& f, std::string_view name)
        {
            return lookup(entries(f.textures, f.overlay, [](const ovl::Overlay& o, const ovl::Part& p) { return pairs<ovl::FlexiTexture>(ovl::read_flexi_textures(o, p), &ovl::FlexiTextureItem::texture); }), name);
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
        std::string key = found->first + ":" + std::string(name);
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

    void ModelLoader::set_texture(ModelPart& part, std::string_view name, std::string_view near)
    {
        part.texture = texture_key(name, near);
        const auto picture = pictures_.find(part.texture);
        part.slots = picture != pictures_.end() ? picture->second.slots : 0;
    }

    std::optional<std::size_t> level_at(const ObjectModel& model, float distance, float scale)
    {
        for (std::size_t i = 0; i < model.levels.size(); ++i)
        {
            if (distance <= model.levels[i].distance * scale)
                return i;
        }
        return std::nullopt;
    }

    std::optional<ObjectModel> ModelLoader::load(std::string_view symbol, std::string_view overlay, bool modelled_plants)
    {
        const std::string hint = index_.overlay(overlay).value_or(std::string(overlay));
        const auto item_file = files_->holding(tagged(symbol, kItemTag), hint);
        if (!item_file)
            return std::nullopt;
        const ovl::SceneryItem* item = item_in(*item_file->second, symbol);
        if (item == nullptr)
            return std::nullopt;
        const std::string near = item_file->first;

        // The visuals that can be read, with the file each is in.
        std::vector<std::pair<const ovl::SceneryVisual*, std::string>> visuals;
        for (const std::string_view visual_name : item->visuals)
        {
            const auto visual_file = files_->holding(tagged(visual_name, kVisualTag), near);
            if (!visual_file)
                continue;
            if (const ovl::SceneryVisual* visual = visual_in(*visual_file->second, visual_name))
                visuals.emplace_back(visual, visual_file->first);
        }
        if (visuals.empty())
            return std::nullopt;
        const auto& [visual, visual_path] = modelled_plants ? visuals.back() : visuals.front();

        ObjectModel model;
        model.positioning = item->positioning;
        model.sway = visual->sway;
        model.support_group = item->group;
        if (!item->squares.empty())
            model.lowest_level = std::ranges::min(item->squares, { }, &ovl::SceneryItemSquare::min_height).min_height;
        model.supports = support_footprint(*item);
        for (const ovl::SceneryVisualLod& lod : visual->lods)
        {
            ModelLevel level;
            level.distance = lod.distance;
            if (lod.mesh_type == static_cast<std::uint32_t>(ovl::LodMeshType::StaticShape))
            {
                const auto file = files_->holding(tagged(lod.static_shape, kStaticTag), visual_path);
                const ovl::StaticShape* shape = file ? static_in(*file->second, lod.static_shape) : nullptr;
                if (shape == nullptr)
                    continue;
                for (const ovl::ShapeMesh& mesh : shape->meshes)
                {
                    level.parts.push_back(part_of(mesh, [](const ovl::ShapeMesh& m, std::size_t i) { return m.index(i); }));
                    set_texture(level.parts.back(), mesh.texture, file->first);
                }
            }
            else if (lod.mesh_type == static_cast<std::uint32_t>(ovl::LodMeshType::BoneShape))
            {
                const auto file = files_->holding(tagged(lod.bone_shape, kBoneTag), visual_path);
                const ovl::BoneShape* shape = file ? bone_in(*file->second, lod.bone_shape) : nullptr;
                if (shape == nullptr)
                    continue;
                for (const ovl::BoneMesh& mesh : shape->meshes)
                {
                    level.parts.push_back(part_of(mesh, [](const ovl::BoneMesh& m, std::size_t i) { return static_cast<std::uint32_t>(m.index(i)); }));
                    set_texture(level.parts.back(), mesh.texture, file->first);
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
        if (model.levels.empty())
            return std::nullopt;
        return model;
    }

} // namespace rct3::park
