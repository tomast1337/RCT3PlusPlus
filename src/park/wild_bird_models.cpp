// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/wild_bird_models.hpp"

#include "core/maybe.hpp"
#include "core/types.hpp"
#include "ovl/overlay.hpp"
#include "ovl/texture/texture.hpp"
#include "park/person/models.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace rct3::park
{

    namespace
    {
        std::string lower(std::string_view s)
        {
            std::string out(s);
            std::ranges::transform(out, out.begin(), [](char c) { return strings::ascii_lower(c); });
            return out;
        }

        /// A motion's name without the number the files put in front of it, in lower case.
        std::string bare(std::string_view name)
        {
            while (!name.empty() && (std::isdigit(static_cast<unsigned char>(name.front())) || name.front() == '_'))
                name.remove_prefix(1);
            return lower(name);
        }

        constexpr std::array<std::string_view, static_cast<usize>(BirdMotion::Count)> kMotionNames {
            "swim", "fly", "glide", "takeoff", "land", "standidle", "idlepeck", "takeoffground", "landground",
        };

        /// Where in the two by two picture the colouring `palette` starts, as a shift of texture coordinates.
        glm::vec2 palette_shift(u8 palette)
        {
            return { 0.5f * static_cast<f32>(palette & 1), -0.5f * static_cast<f32>(palette >> 1 & 1) };
        }
    } // namespace

    std::optional<BirdModel> read_bird_model(const std::filesystem::path& path, std::string_view mesh_name, std::string_view picture)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::nullopt;
        BirdModel model;
        bool found = false;
        for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (ovl::FrameMeshItem& item : ovl::read_frame_meshes(*overlay, *part))
            {
                if (item.mesh && !found && lower(item.name) == lower(mesh_name))
                {
                    model.mesh = std::move(*item.mesh);
                    found = true;
                }
            }
        }
        if (!found || model.mesh.animations.empty())
            return std::nullopt;
        model.motions.fill(BirdModel::kNone);
        for (usize m = 0; m < model.motions.size(); ++m)
            for (usize a = 0; a < model.mesh.animations.size(); ++a)
                if (bare(model.mesh.animations[a].name) == kMotionNames[m])
                    model.motions[m] = a;
        for (ovl::FrameAnimation& a : model.mesh.animations)
            a.name = { };
        const auto pixels = ovl::texture::TexturePixels::index(*overlay);
        for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const auto& item : ovl::texture::read_textures(*overlay, *part))
            {
                if (item.texture && lower(item.name) == lower(picture) && model.picture.pixels.empty())
                {
                    if (auto image = pixels.first_frame(*item.texture))
                        model.picture = std::move(*image);
                }
            }
        }
        if (model.picture.pixels.empty())
            return std::nullopt;
        model.picture_key = "bird|" + lower(picture);
        return model;
    }

    f32 motion_seconds(const BirdModel& model, BirdMotion motion)
    {
        const usize index = model.motions[static_cast<usize>(motion)];
        return index < model.mesh.animations.size() && !model.mesh.animations[index].times.empty() ? model.mesh.animations[index].times.back() : 0.0f;
    }

    object::ObjectMesh bird_mesh(std::span<const PlacedBird> birds)
    {
        struct Run
        {
            std::vector<GroundVertex> vertices;
            std::vector<u32> indices;
        };
        std::map<std::string, Run> runs;
        object::ObjectMesh mesh;
        for (const PlacedBird& bird : birds)
        {
            if (bird.model == nullptr)
                continue;
            const ovl::FrameMesh& frames = bird.model->mesh;
            usize index = bird.model->motions[static_cast<usize>(bird.motion)];
            if (index >= frames.animations.size())
                index = 0;
            const ovl::FrameAnimation& motion = frames.animations[index];
            if (motion.times.empty())
                continue;
            const person::KeyBlend blend = person::blend_keys(motion.times, bird.time.value, repeats(bird.motion));
            const ovl::FrameKey from = ovl::decode_key(frames, motion, blend.from);
            const ovl::FrameKey to = ovl::decode_key(frames, motion, blend.to);
            if (from.points.size() != frames.points || to.points.size() != frames.points || from.normals.size() != frames.normals || to.normals.size() != frames.normals)
                continue;
            Run& run = runs[bird.model->picture_key];
            const auto base = static_cast<u32>(run.vertices.size());
            const glm::mat3 turn { bird.world };
            const glm::vec2 shift = palette_shift(bird.palette);
            const auto mixed = [&](const std::array<f32, 3>& a, const std::array<f32, 3>& b) { return glm::mix(glm::vec3(a[0], a[1], a[2]), glm::vec3(b[0], b[1], b[2]), blend.along); };
            for (const ovl::FrameCorner& corner : frames.corners)
            {
                if (corner.point >= frames.points || corner.normal >= frames.normals)
                {
                    run.vertices.push_back({ glm::vec3(bird.world[3]), glm::vec4(1.0f), { }, { 0.0f, 1.0f, 0.0f } });
                    continue;
                }
                const glm::vec3 at = mixed(from.points[corner.point], to.points[corner.point]);
                const glm::vec3 normal = mixed(from.normals[corner.normal], to.normals[corner.normal]);
                run.vertices.push_back({ glm::vec3(bird.world * glm::vec4(at, 1.0f)), glm::vec4(1.0f), glm::vec2(corner.uv[0], corner.uv[1]) + shift, turn * normal });
            }
            for (const u16 i : frames.indices)
                run.indices.push_back(base + (i < frames.corners.size() ? i : 0));
            ++mesh.drawn;
        }
        for (const auto& [picture, run] : runs)
        {
            const auto base = static_cast<u32>(mesh.vertices.size());
            mesh.batches.push_back({ .texture = picture, .picture = picture, .blend = object::PartBlend::Opaque, .first = static_cast<u32>(mesh.indices.size()), .count = static_cast<u32>(run.indices.size()), .one_sided = false });
            mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
            for (const u32 i : run.indices)
                mesh.indices.push_back(base + i);
        }
        return mesh;
    }

} // namespace rct3::park
