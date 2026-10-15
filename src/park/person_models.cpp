// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_models.hpp"

#include "ovl/overlay.hpp"
#include "ovl/person_parts.hpp"
#include "ovl/skeleton.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "park/person_motions.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
            return std::string(name) + ":" + std::string(tag);
        }

        /// Items of one type of an open file, by lower-case name.
        template<typename Item> using ByName = std::map<std::string, Item, std::less<>>;

    } // namespace

    KeyBlend blend_keys(std::span<const float> times, float time, bool repeats)
    {
        const std::size_t n = times.size();
        if (n <= 1 || !(time > 0))
            return { };
        const float length = times[n - 1];
        if (time >= length)
        {
            if (!repeats || !(length > 0))
                return { n - 1, n - 1, 0 };
            time = std::fmod(time, length);
        }
        const auto i = static_cast<std::size_t>(std::ranges::upper_bound(times, time) - times.begin());
        if (i >= n)
            return { n - 1, n - 1, 0 };
        const float before = i > 0 ? times[i - 1] : 0.0f;
        const float span = times[i] - before;
        return { i, i + 1 == n ? 0 : i + 1, span > 0 ? (time - before) / span : 0.0f };
    }

    MotionIndex::MotionIndex(std::span<const std::string_view> names)
    {
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            const auto number = motion_number(names[i]);
            if (!number || *number < 0)
                continue;
            const auto at = static_cast<std::size_t>(*number);
            if (by_number_.size() <= at)
                by_number_.resize(at + 1, 0);
            // The first motion of a name wins, as several spellings can share a number.
            if (by_number_[at] == 0)
                by_number_[at] = static_cast<std::uint16_t>(i);
        }
    }

    std::size_t MotionIndex::operator[](std::int32_t number) const noexcept
    {
        if (number < 0 || static_cast<std::size_t>(number) >= by_number_.size())
            return 0;
        return by_number_[static_cast<std::size_t>(number)];
    }

    struct PersonLibrary::Files
    {
        struct Open
        {
            explicit Open(ovl::Overlay&& file)
                : overlay(std::move(file))
            {
            }

            ovl::Overlay overlay;
            std::optional<ByName<ovl::BodySection>> sets;
            std::optional<ByName<ovl::BodyPiece>> pieces;
            std::optional<ByName<ovl::HeadRig>> heads;
            std::optional<ByName<ovl::FrameMesh>> meshes;
            std::optional<ByName<ovl::Skeleton>> skeletons;
            std::optional<ByName<ovl::SimpleAnim>> motions;
            std::optional<ByName<ovl::SimpleSkin>> skins;
            std::optional<ByName<ovl::Texture>> textures;
            std::optional<ovl::TexturePixels> pixels;
        };

        AssetIndex parts;
        std::map<std::string, std::unique_ptr<Open>, std::less<>> open;

        Open* file(std::string_view path)
        {
            if (const auto it = open.find(path); it != open.end())
                return it->second.get();
            auto overlay = ovl::Overlay::load(parts.file(path));
            if (!overlay)
                return nullptr;
            auto opened = std::make_unique<Open>(std::move(*overlay));
            Open* raw = opened.get();
            open.emplace(std::string(path), std::move(opened));
            return raw;
        }

        /// Reads every item of one type in `file`, keyed by lower-case name, once.
        template<typename Value, typename Item, typename Member> const ByName<Value>& items(Open& file, std::optional<ByName<Value>>& cache, std::vector<Item> (*read)(const ovl::Overlay&, const ovl::Part&), Member member)
        {
            if (!cache)
            {
                cache.emplace();
                for (const ovl::Part* part : { &file.overlay.common(), file.overlay.unique() })
                {
                    if (part == nullptr)
                        continue;
                    for (auto& item : read(file.overlay, *part))
                    {
                        if (auto& value = item.*member)
                            cache->try_emplace(lower(item.name), std::move(*value));
                    }
                }
            }
            return *cache;
        }

        /// The item `name` of type `tag`, from the file nearest `near` that holds it.
        template<typename Value> const Value* find_item(std::string_view name, std::string_view tag, std::string_view near, auto&& of_file)
        {
            const auto path = parts.find(tagged(name, tag), near);
            if (!path)
                return nullptr;
            Open* opened = file(*path);
            if (!opened)
                return nullptr;
            const ByName<Value>& all = of_file(*opened);
            const auto it = all.find(lower(name));
            return it != all.end() ? &it->second : nullptr;
        }
    };

    PersonLibrary::PersonLibrary(const std::filesystem::path& root, std::string_view folder)
        : files_(std::make_unique<Files>())
    {
        const std::array folders { folder };
        files_->parts = AssetIndex::scan(root, folders);
    }

    PersonLibrary::~PersonLibrary() = default;
    PersonLibrary::PersonLibrary(PersonLibrary&&) noexcept = default;
    PersonLibrary& PersonLibrary::operator=(PersonLibrary&&) noexcept = default;

    void PersonLibrary::close_files()
    {
        files_->open.clear();
    }

    const PartSet* PersonLibrary::part_set(std::string_view symbol)
    {
        const std::string key = lower(symbol);
        auto it = sets_.find(key);
        if (it == sets_.end())
            it = sets_.emplace(key, read_set(symbol)).first;
        return it->second ? &*it->second : nullptr;
    }

    std::optional<PartSet> PersonLibrary::read_set(std::string_view symbol)
    {
        const auto path = files_->parts.find(tagged(symbol, "ppg"), "");
        if (!path)
            return std::nullopt;
        Files::Open* file = files_->file(*path);
        if (!file)
            return std::nullopt;
        const auto& sets = files_->items(*file, file->sets, &ovl::read_body_sections, &ovl::BodySectionItem::section);
        const auto found = sets.find(lower(symbol));
        if (found == sets.end())
            return std::nullopt;
        const ovl::BodySection section = found->second;

        PartSet set;
        for (std::size_t i = 0; i < kPersonLevels; ++i)
        {
            auto level = read_level(section.levels[i].fixed, section.levels[i].optional, *path);
            auto swim = read_level(section.swim_levels[i].fixed, section.swim_levels[i].optional, *path);
            if (!level || !swim)
                return std::nullopt;
            set.levels[i] = std::move(*level);
            set.swim_levels[i] = std::move(*swim);
        }
        if (!section.head.empty())
            set.head = read_head(section.head, *path);
        if (!section.swim_head.empty())
            set.swim_head = read_head(section.swim_head, *path);
        return set;
    }

    std::optional<PartLevel> PersonLibrary::read_level(std::span<const std::string_view> fixed, std::span<const std::string_view> optional, std::string_view near)
    {
        PartLevel level;
        for (const auto& [names, out] : { std::pair { fixed, &level.fixed }, std::pair { optional, &level.optional } })
        {
            for (const std::string_view name : names)
            {
                // A piece that cannot be read is left out; an optional one keeps its place so later bits still
                // pick the right pieces.
                if (const auto piece = read_piece(name, near))
                    out->push_back(*piece);
                else if (out == &level.optional)
                    out->push_back(kNoPiece);
            }
        }
        return level;
    }

    std::optional<std::size_t> PersonLibrary::read_piece(std::string_view name, std::string_view near)
    {
        Files& f = *files_;
        const auto piece_file = f.parts.find(tagged(name, "prt"), near);
        if (!piece_file)
            return std::nullopt;
        // Pieces of different sets can share a name, so they are told apart by their file.
        const std::string key = *piece_file + "|" + lower(name);
        if (const auto it = loaded_pieces_.find(key); it != loaded_pieces_.end())
            return it->second;
        std::optional<std::size_t> result;
        const auto* piece = f.find_item<ovl::BodyPiece>(name, "prt", near, [&](Files::Open& o) -> const auto& { return f.items(o, o.pieces, &ovl::read_body_pieces, &ovl::BodyPieceItem::piece); });
        if (piece)
        {
            const auto* mesh = f.find_item<ovl::FrameMesh>(piece->mesh, "mms", *piece_file, [&](Files::Open& o) -> const auto& { return f.items(o, o.meshes, &ovl::read_frame_meshes, &ovl::FrameMeshItem::mesh); });
            // Sockets for held things carry no shape of their own.
            if (mesh && mesh->tracks == 0)
            {
                PieceModel model;
                model.channel = piece->channel;
                model.mesh = *mesh;
                std::vector<std::string_view> names;
                for (const ovl::FrameAnimation& a : model.mesh.animations)
                    names.push_back(a.name);
                model.motions = MotionIndex(names);
                for (ovl::FrameAnimation& a : model.mesh.animations)
                    a.name = { };
                if (!piece->texture.empty())
                    model.texture = read_picture(piece->texture, *piece_file);
                result = pieces_.size();
                pieces_.push_back(std::move(model));
            }
        }
        loaded_pieces_.emplace(key, result);
        return result;
    }

    std::optional<std::size_t> PersonLibrary::read_head(std::string_view name, std::string_view near)
    {
        Files& f = *files_;
        const auto found_file = f.parts.find(tagged(name, "phd"), near);
        if (!found_file)
            return std::nullopt;
        const std::string& rig_file = *found_file;
        const std::string key = rig_file + "|" + lower(name);
        if (const auto it = loaded_heads_.find(key); it != loaded_heads_.end())
            return it->second;
        std::optional<std::size_t> result;
        const auto* rig = f.find_item<ovl::HeadRig>(name, "phd", near, [&](Files::Open& o) -> const auto& { return f.items(o, o.heads, &ovl::read_head_rigs, &ovl::HeadRigItem::head); });
        const auto* skeleton = rig ? f.find_item<ovl::Skeleton>(rig->bones, "sal", rig_file, [&](Files::Open& o) -> const auto& { return f.items(o, o.skeletons, &ovl::read_skeletons, &ovl::SkeletonItem::skeleton); }) : nullptr;
        if (rig && skeleton)
        {
            const auto skeleton_file = f.parts.find(tagged(rig->bones, "sal"), rig_file).value_or(rig_file);
            HeadModel head;
            head.parents = skeleton->parents;
            head.motion_index = MotionIndex(skeleton->animation_names);
            for (const std::string_view motion : skeleton->animations)
            {
                const auto* anim = motion.empty() ? nullptr : f.find_item<ovl::SimpleAnim>(motion, "san", skeleton_file, [&](Files::Open& o) -> const auto& { return f.items(o, o.motions, &ovl::read_simple_anims, &ovl::SimpleAnimItem::anim); });
                head.motions.push_back(anim ? std::optional(*anim) : std::nullopt);
            }
            for (std::size_t level = 0; level < head.skins.size(); ++level)
            {
                const std::string_view skin = rig->skins[level * ovl::kHeadFaces];
                if (const auto* found = skin.empty() ? nullptr : f.find_item<ovl::SimpleSkin>(skin, "ssk", rig_file, [&](Files::Open& o) -> const auto& { return f.items(o, o.skins, &ovl::read_simple_skins, &ovl::SimpleSkinItem::skin); }))
                    head.skins[level] = *found;
            }
            if (!rig->texture.empty())
                head.texture = read_picture(rig->texture, rig_file);
            result = heads_.size();
            heads_.push_back(std::move(head));
        }
        loaded_heads_.emplace(key, result);
        return result;
    }

    std::string PersonLibrary::read_picture(std::string_view name, std::string_view near)
    {
        Files& f = *files_;
        const auto path = f.parts.find(tagged(name, "tex"), near);
        if (!path)
            return { };
        std::string key = *path + "|" + lower(name);
        if (pictures_.contains(key))
            return key;
        Files::Open* file = f.file(*path);
        if (!file)
            return { };
        const auto& textures = f.items(*file, file->textures, &ovl::read_textures, &ovl::TextureItem::texture);
        const auto texture = textures.find(lower(name));
        if (texture == textures.end())
            return { };
        if (!file->pixels)
            file->pixels = ovl::TexturePixels::index(file->overlay);
        auto image = file->pixels->first_frame(texture->second);
        if (!image)
            return { };
        pictures_.emplace(key, std::move(*image));
        return key;
    }

} // namespace rct3::park
