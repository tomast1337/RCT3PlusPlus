// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/animal_models.hpp"

#include "core/maybe.hpp"
#include "ovl/animal_model.hpp"
#include "ovl/animal_species.hpp"
#include "ovl/model_anim.hpp"
#include "ovl/overlay.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "park/asset_index.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <format>

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

        std::string tagged(std::string_view name, std::string_view tag)
        {
            return std::format("{}:{}", name, tag);
        }

        /// The blending and sides a texture style asks for: masked styles cut out, other alpha styles blend,
        /// and those ending "DS" are drawn from both sides.
        std::pair<PartBlend, bool> style_look(std::string_view style)
        {
            const std::string s = lower(style);
            const PartBlend blend = s.starts_with("sialphamask") ? PartBlend::Cutout : s.starts_with("sialpha") ? PartBlend::Blended : PartBlend::Opaque;
            return { blend, !s.ends_with("ds") };
        }

        glm::quat quat_of(const std::array<float, 4>& xyzw)
        {
            return glm::quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
        }

        glm::mat4 bone_local(const glm::vec3& position, const glm::quat& rotation)
        {
            return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
        }

        /// Every bone's place in model space, given each one's place from its parent.
        void chain(std::span<const std::int32_t> parents, std::span<glm::mat4> world)
        {
            for (std::size_t b = 0; b < parents.size(); ++b)
            {
                if (parents[b] >= 0)
                    world[b] = world[static_cast<std::size_t>(parents[b])] * world[b];
            }
        }

    } // namespace

    float motion_seconds(const AnimalBody& body, ovl::AnimalMotion motion)
    {
        const auto slot = static_cast<std::size_t>(motion);
        return slot < body.motions.size() && body.motions[slot] ? body.motions[slot]->seconds : 0.0f;
    }

    std::vector<glm::mat4> animal_pose(const AnimalBody& body, ovl::AnimalMotion motion, float time)
    {
        const std::size_t n = body.parents.size();
        std::vector<glm::vec3> positions = body.rest_positions;
        std::vector<glm::quat> rotations = body.rest_rotations;
        positions.resize(n);
        rotations.resize(n, glm::quat(1, 0, 0, 0));
        std::vector<glm::mat4> rest(n);
        for (std::size_t b = 0; b < n; ++b)
            rest[b] = bone_local(positions[b], rotations[b]);
        chain(body.parents, rest);

        const auto slot = static_cast<std::size_t>(motion);
        if (slot < body.motions.size() && body.motions[slot] && body.motions[slot]->frames > 0)
        {
            const AnimalClip& clip = *body.motions[slot];
            const float last = static_cast<float>(clip.frames - 1);
            float frame = clip.seconds > 0 && std::isfinite(time) ? std::fmod(std::max(time, 0.0f), clip.seconds) * kAnimalFramesPerSecond : 0.0f;
            frame = std::clamp(frame, 0.0f, last);
            const auto from = static_cast<std::size_t>(frame);
            const std::size_t to = std::min<std::size_t>(from + 1, clip.frames - 1);
            const float along = frame - static_cast<float>(from);
            for (std::size_t k = 0; k < clip.moving.size(); ++k)
            {
                const auto b = static_cast<std::size_t>(clip.moving[k]);
                if (b < n && (to + 1) * clip.moving.size() <= clip.positions.size())
                    positions[b] = glm::mix(clip.positions[from * clip.moving.size() + k], clip.positions[to * clip.moving.size() + k], along);
            }
            for (std::size_t k = 0; k < clip.turning.size(); ++k)
            {
                const auto b = static_cast<std::size_t>(clip.turning[k]);
                if (b < n && (to + 1) * clip.turning.size() <= clip.rotations.size())
                    rotations[b] = glm::slerp(clip.rotations[from * clip.turning.size() + k], clip.rotations[to * clip.turning.size() + k], along);
            }
        }

        std::vector<glm::mat4> posed(n);
        for (std::size_t b = 0; b < n; ++b)
            posed[b] = bone_local(positions[b], rotations[b]);
        chain(body.parents, posed);
        for (std::size_t b = 0; b < n; ++b)
            posed[b] = posed[b] * glm::inverse(rest[b]);
        return posed;
    }

    struct AnimalLibrary::Files
    {
        struct Open
        {
            explicit Open(ovl::Overlay&& file)
                : overlay(std::move(file))
            {
            }
            ovl::Overlay overlay;
            std::optional<ovl::TexturePixels> pixels;
        };

        AssetIndex index;
        std::map<std::string, std::unique_ptr<Open>, std::less<>> open;

        Open* file(std::string_view path)
        {
            if (const auto it = open.find(path); it != open.end())
                return it->second.get();
            auto overlay = ovl::Overlay::load(index.file(path));
            if (!overlay)
                return nullptr;
            auto opened = std::make_unique<Open>(std::move(*overlay));
            Open* raw = opened.get();
            open.emplace(std::string(path), std::move(opened));
            return raw;
        }

        /// The file nearest `near` holding `name` of type `tag`, and the item named so that `read` finds in it.
        template<typename Item> std::pair<Open*, std::optional<Item>> find(std::string_view name, std::string_view tag, std::string_view near, auto read, auto member, std::string* path_out = nullptr)
        {
            const auto path = index.find(tagged(name, tag), near);
            Open* opened = path ? file(*path) : nullptr;
            if (!opened)
                return { nullptr, std::nullopt };
            if (path_out)
                *path_out = *path;
            for (const auto part : { maybe(&opened->overlay.common()), opened->overlay.unique() })
            {
                if (!part)
                    continue;
                for (auto& item : read(opened->overlay, *part))
                {
                    if (strings::equals_ignoring_case(item.name, name) && item.*member)
                        return { opened, std::move(*(item.*member)) };
                }
            }
            return { opened, std::nullopt };
        }
    };

    AnimalLibrary::AnimalLibrary(const std::filesystem::path& root, std::span<const std::string_view> folders)
        : files_(std::make_unique<Files>())
    {
        files_->index = AssetIndex::scan(root, folders);
    }

    AnimalLibrary::~AnimalLibrary() = default;
    AnimalLibrary::AnimalLibrary(AnimalLibrary&&) noexcept = default;
    AnimalLibrary& AnimalLibrary::operator=(AnimalLibrary&&) noexcept = default;

    void AnimalLibrary::close_files()
    {
        files_->open.clear();
    }

    std::optional<const AnimalBody&> AnimalLibrary::body(std::string_view species, bool adult, bool male)
    {
        const std::string key = std::format("{}|{}{}", lower(species), adult ? 'a' : 'y', male ? 'm' : 'f');
        auto it = bodies_.find(key);
        if (it == bodies_.end())
            it = bodies_.emplace(key, read_body(species, adult, male)).first;
        return it->second ? std::optional<const AnimalBody&>(*it->second) : std::nullopt;
    }

    std::optional<AnimalBody> AnimalLibrary::read_body(std::string_view species, bool adult, bool male)
    {
        Files& f = *files_;
        std::string species_file;
        const auto [unused, found] = f.find<ovl::AnimalSpecies>(species, "was", "", &ovl::read_animal_species, &ovl::AnimalSpeciesItem::species, &species_file);
        if (!found)
            return std::nullopt;
        const auto wanted = static_cast<std::size_t>(adult ? (male ? ovl::AnimalAge::AdultMale : ovl::AnimalAge::AdultFemale) : (male ? ovl::AnimalAge::YoungMale : ovl::AnimalAge::YoungFemale));
        // The one asked for, the other sex of the same age, then any.
        const std::optional<ovl::AnimalVariant>* variant = nullptr;
        for (const std::size_t i : { wanted, wanted ^ 1, std::size_t { 0 }, std::size_t { 1 }, std::size_t { 2 }, std::size_t { 3 } })
        {
            if (found->variants[i] && !found->variants[i]->model.empty())
            {
                variant = &found->variants[i];
                break;
            }
        }
        if (!variant)
            return std::nullopt;

        std::string model_file;
        const auto [model_open, model] = f.find<ovl::AnimalModel>((*variant)->model, "mdl", species_file, &ovl::read_animal_models, &ovl::AnimalModelItem::model, &model_file);
        if (!model)
            return std::nullopt;

        AnimalBody body;
        std::vector<std::string> bone_names;
        for (const ovl::AnimalBone& bone : model->bones)
        {
            body.parents.push_back(bone.parent);
            body.rest_positions.emplace_back(bone.position[0], bone.position[1], bone.position[2]);
            body.rest_rotations.push_back(quat_of(bone.rotation));
            bone_names.push_back(lower(bone.name));
        }
        const auto bone_of = [&](std::string_view name) -> std::int32_t {
            const auto at = std::ranges::find(bone_names, lower(name));
            return at == bone_names.end() ? -1 : static_cast<std::int32_t>(at - bone_names.begin());
        };

        for (const auto& level : model->levels)
        {
            auto& pieces = body.levels.emplace_back();
            for (const ovl::BoneMesh& mesh : level)
            {
                AnimalPiece piece;
                if (!mesh.texture.empty())
                {
                    // A material names a texture, often kept in another file of the species.
                    std::string texture_file;
                    const auto [texture_open, texture] = f.find<ovl::Texture>(mesh.texture, "tex", model_file, &ovl::read_textures, &ovl::TextureItem::texture, &texture_file);
                    if (texture)
                    {
                        std::tie(piece.blend, piece.one_sided) = style_look(texture->style);
                        std::string key = std::format("{}|{}", texture_file, lower(mesh.texture));
                        if (!pictures_.contains(key))
                        {
                            if (!texture_open->pixels)
                                texture_open->pixels = ovl::TexturePixels::index(texture_open->overlay);
                            if (auto image = texture_open->pixels->first_frame(*texture))
                                pictures_.emplace(key, std::move(*image));
                        }
                        if (pictures_.contains(key))
                            piece.picture = std::move(key);
                    }
                }
                for (std::uint32_t v = 0; v < mesh.vertex_count; ++v)
                {
                    const ovl::BoneVertex b = mesh.vertex(v);
                    piece.vertices.push_back({ { b.position[0], b.position[1], b.position[2] }, { b.normal[0], b.normal[1], b.normal[2] }, b.bones, b.weights, { b.texcoord[0], b.texcoord[1] } });
                }
                for (std::uint32_t i = 0; i < mesh.index_count; ++i)
                    piece.indices.push_back(mesh.index(i));
                pieces.push_back(std::move(piece));
            }
        }

        body.motions.resize(static_cast<std::size_t>(ovl::AnimalMotion::TurnRight90) + 1);
        std::string motions_file;
        const auto [motions_open, motions] = (*variant)->motions.empty() ? std::pair<Files::Open*, std::optional<ovl::AnimalMotions>> { } : f.find<ovl::AnimalMotions>((*variant)->motions, "wad", model_file, &ovl::read_animal_motions, &ovl::AnimalMotionsItem::motions, &motions_file);
        for (std::size_t slot = 0; motions && slot < motions->animations.size() && slot < body.motions.size(); ++slot)
        {
            const std::string_view name = motions->animations[slot];
            if (name.empty())
                continue;
            const auto [anim_open, anim] = f.find<ovl::ModelAnim>(name, "modelanim", motions_file, &ovl::read_model_anims, &ovl::ModelAnimItem::anim);
            if (!anim || anim->frame_count == 0)
                continue;
            AnimalClip clip { .seconds = anim->duration, .frames = anim->frame_count };
            for (const std::string_view bone : anim->position_bones)
                clip.moving.push_back(bone_of(bone));
            for (const std::string_view bone : anim->rotation_bones)
                clip.turning.push_back(bone_of(bone));
            for (const auto& p : anim->positions)
                clip.positions.emplace_back(p[0], p[1], p[2]);
            for (const auto& r : anim->rotations)
                clip.rotations.push_back(quat_of(r));
            body.motions[slot] = std::move(clip);
        }
        return body;
    }

} // namespace rct3::park
