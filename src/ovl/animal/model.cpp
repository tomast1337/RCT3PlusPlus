// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal/model.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>
#include <unordered_map>

namespace rct3::ovl::animal
{

    namespace
    {

        constexpr std::string_view kTag = "mdl";
        constexpr usize kHeaderSize = 0x50;
        constexpr u32 kMaxBones = 256; ///< Vertices name bones by one byte
        constexpr u32 kMaxCount = 1u << 16;

        // Header fields.
        constexpr usize kFlags = 0x00;
        constexpr usize kLevelCount = 0x04;    // u8
        constexpr usize kStoredLevels = 0x05;  // u8: levels kept in the first chunk
        constexpr usize kMaterialCount = 0x06; // u16
        constexpr usize kBoundsMin = 0x14;
        constexpr usize kBoundsMax = 0x20;
        constexpr usize kPrefixWords = 0x4C; // u32: extra words after the chunk's counts
        /// Flags of layouts not seen in any file: levels kept elsewhere and other name lists.
        constexpr u32 kUnsupportedFlags = 0x180000;

        constexpr usize kBoneSize = 96;       // offset (4 floats), turn (4 floats), inverse rest matrix (16)
        constexpr usize kLevelSize = 20;      // two u16 counts, bone count, three pointers
        constexpr usize kMeshSize = 32;       // format, index count, index sets, vertex count, then runtime fields
        constexpr usize kGroupSize = 16;      // material, two list lengths, then pointers
        constexpr u32 kVertexFormat = 0x1305; // position, normal, four bones and weights, colour, one uv

        /// Reads the chunk front to back, failing once anything would run past its end.
        class Cursor
        {
        public:
            explicit Cursor(std::span<const std::byte> bytes)
                : bytes_(bytes)
            {
            }

            bool ok() const noexcept
            {
                return ok_;
            }
            usize at() const noexcept
            {
                return at_;
            }
            void align() noexcept
            {
                at_ = (at_ + 15) & ~usize { 15 };
                check(0);
            }
            std::span<const std::byte> take(usize size) noexcept
            {
                if (!check(size))
                    return { };
                const auto out = bytes_.subspan(at_, size);
                at_ += size;
                return out;
            }
            u32 read_u32() noexcept
            {
                return word<u32>();
            }
            u16 read_u16() noexcept
            {
                return word<u16>();
            }

        private:
            bool check(usize size) noexcept
            {
                if (at_ > bytes_.size() || bytes_.size() - at_ < size)
                    ok_ = false;
                return ok_;
            }
            template<typename T> T word() noexcept
            {
                T v { };
                if (const auto b = take(sizeof v); !b.empty())
                    std::memcpy(&v, b.data(), sizeof v);
                return v;
            }

            std::span<const std::byte> bytes_;
            usize at_ = 0;
            bool ok_ = true;
        };

        f32 f32_at(std::span<const std::byte> b, usize at)
        {
            f32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::span<const std::byte> chunk_bytes(const Part& part, const Chunk& chunk)
        {
            if (chunk.file_offset > part.bytes.size() || part.bytes.size() - chunk.file_offset < chunk.size)
                return { };
            return std::span(part.bytes).subspan(chunk.file_offset, chunk.size);
        }

        std::optional<AnimalModel> read_one(const AddressSpace& space, const Part& part, usize row)
        {
            if (row >= part.loaders.extra.size() || part.loaders.extra[row].size() != 2)
                return std::nullopt;
            const auto header = space.bytes_at(part.loaders.entries[row].data, kHeaderSize);
            if (!header)
                return std::nullopt;
            u32 flags = 0;
            std::memcpy(&flags, header->data() + kFlags, sizeof flags);
            const auto level_count = std::to_integer<u32>((*header)[kLevelCount]);
            const auto stored_levels = std::to_integer<u32>((*header)[kStoredLevels]);
            u16 material_count = 0;
            std::memcpy(&material_count, header->data() + kMaterialCount, sizeof material_count);
            u32 prefix_words = 0;
            std::memcpy(&prefix_words, header->data() + kPrefixWords, sizeof prefix_words);
            if ((flags & kUnsupportedFlags) != 0 || stored_levels != level_count || prefix_words > kMaxCount)
                return std::nullopt;

            AnimalModel model;
            for (usize k = 0; k < 3; ++k)
            {
                model.bounds_min[k] = f32_at(*header, kBoundsMin + k * 4);
                model.bounds_max[k] = f32_at(*header, kBoundsMax + k * 4);
            }

            const auto data = chunk_bytes(part, part.loaders.extra[row][0]);
            const auto names = chunk_bytes(part, part.loaders.extra[row][1]);
            Cursor c(data);
            const u32 bone_count = c.read_u32();
            c.read_u32(); // Lengths of two lookup tables the game builds itself.
            const u32 extra_links = c.read_u32();
            c.read_u32();
            if (!c.ok() || bone_count > kMaxBones || extra_links != 0)
                return std::nullopt;
            c.take(usize { prefix_words } * 4);
            c.align();

            // Bones, then a parent and a sibling link for each.
            const auto bones = c.take(usize { bone_count } * kBoneSize);
            const auto links = c.take(usize { bone_count } * 4);
            if (!c.ok())
                return std::nullopt;
            std::string_view name_text(reinterpret_cast<const char*>(names.data()), names.size());
            model.bones.reserve(bone_count);
            for (u32 i = 0; i < bone_count; ++i)
            {
                AnimalBone bone;
                const usize end = name_text.find('\0');
                if (end == std::string_view::npos)
                    return std::nullopt;
                bone.name = name_text.substr(0, end);
                name_text.remove_prefix(end + 1);
                i16 parent = 0;
                std::memcpy(&parent, links.data() + usize { i } * 4, sizeof parent);
                if (parent < -1 || parent >= static_cast<i32>(i))
                    return std::nullopt;
                if (parent >= 0)
                    bone.parent = BoneIndex { static_cast<usize>(parent) };
                const auto raw = bones.subspan(usize { i } * kBoneSize, kBoneSize);
                for (usize k = 0; k < 3; ++k)
                    bone.position[k] = f32_at(raw, k * 4);
                for (usize k = 0; k < 4; ++k)
                    bone.rotation[k] = f32_at(raw, 16 + k * 4);
                model.bones.push_back(bone);
            }
            c.align();

            // Material names: their lengths, then the names.
            std::vector<u32> lengths(material_count);
            for (u32& length : lengths)
                length = c.read_u32();
            std::vector<std::string_view> materials;
            for (const u32 length : lengths)
            {
                const auto raw = c.take(length);
                if (!c.ok())
                    return std::nullopt;
                const std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
                materials.push_back(text.substr(0, text.find('\0')));
            }
            c.align();

            // Level records, then a word per level for an extra record that no file has.
            const auto levels = c.take(usize { level_count } * kLevelSize);
            for (u32 i = 0; i < level_count; ++i)
            {
                if (c.read_u32() != 0)
                    return std::nullopt;
            }
            c.align();
            if (!c.ok())
                return std::nullopt;

            for (u32 l = 0; l < level_count; ++l)
            {
                const auto record = levels.subspan(usize { l } * kLevelSize, kLevelSize);
                u16 group_count = 0;
                u16 mesh_count = 0;
                u32 bone_mask = 0;
                std::memcpy(&group_count, record.data(), sizeof group_count);
                std::memcpy(&mesh_count, record.data() + 2, sizeof mesh_count);
                std::memcpy(&bone_mask, record.data() + 16, sizeof bone_mask);
                // A level that marks which bones it uses keeps a bit per bone first.
                if (bone_mask != 0)
                    c.take((usize { bone_count } + 31) / 32 * 4);
                const auto meshes = c.take(usize { mesh_count } * kMeshSize);
                c.align();
                if (!c.ok())
                    return std::nullopt;
                std::vector<BoneMesh> level;
                for (u16 m = 0; m < mesh_count; ++m)
                {
                    const auto raw = meshes.subspan(usize { m } * kMeshSize, kMeshSize);
                    u32 format = 0;
                    u32 index_count = 0;
                    u16 index_sets = 0;
                    u16 vertex_count = 0;
                    std::memcpy(&format, raw.data(), sizeof format);
                    std::memcpy(&index_count, raw.data() + 4, sizeof index_count);
                    std::memcpy(&index_sets, raw.data() + 8, sizeof index_sets);
                    std::memcpy(&vertex_count, raw.data() + 10, sizeof vertex_count);
                    if (format != kVertexFormat || index_sets != 1 || index_count > kMaxCount * 3 || index_count % 3 != 0)
                        return std::nullopt;
                    BoneMesh mesh;
                    mesh.vertex_count = vertex_count;
                    mesh.index_count = index_count;
                    mesh.vertices = c.take(usize { vertex_count } * kBoneVertexSize);
                    mesh.indices = c.take(usize { index_count } * 2);
                    c.take((4 - c.at() % 4) % 4);
                    if (!c.ok())
                        return std::nullopt;
                    for (u32 i = 0; i < index_count; ++i)
                    {
                        if (mesh.index(i) >= vertex_count)
                            return std::nullopt;
                    }
                    for (u32 v = 0; v < vertex_count; ++v)
                    {
                        const BoneVertex vertex = mesh.vertex(v);
                        for (usize k = 0; k < 4; ++k)
                            if (vertex.weights[k] != 0 && vertex.bones[k] >= bone_count)
                                return std::nullopt;
                    }
                    level.push_back(mesh);
                }
                // Groups: each names a material and the level's meshes painted with it.
                const auto groups = c.take(usize { group_count } * kGroupSize);
                for (u16 g = 0; g < group_count && c.ok(); ++g)
                {
                    const auto raw = groups.subspan(usize { g } * kGroupSize, kGroupSize);
                    u16 material = 0;
                    u16 listed = 0;
                    u16 others = 0;
                    std::memcpy(&material, raw.data(), sizeof material);
                    std::memcpy(&listed, raw.data() + 2, sizeof listed);
                    std::memcpy(&others, raw.data() + 4, sizeof others);
                    if (material >= materials.size())
                        return std::nullopt;
                    for (u16 k = 0; k < listed; ++k)
                    {
                        const u16 mesh = static_cast<u16>(c.read_u32() & 0xFFFF);
                        if (!c.ok() || mesh >= level.size())
                            return std::nullopt;
                        level[mesh].texture = materials[material];
                    }
                    c.take(usize { others } * 4);
                }
                if (!c.ok())
                    return std::nullopt;
                model.levels.push_back(std::move(level));
            }
            return model;
        }

    } // namespace

    std::vector<AnimalModelItem> read_animal_models(const Overlay& overlay, const Part& part)
    {
        const auto& loaders = part.file.header.loaders;
        const AddressSpace space = overlay.space();

        std::unordered_map<u32, std::string_view> names;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kTag)
                names.emplace(symbol.data, parts.name);
        }

        std::vector<AnimalModelItem> items;
        for (usize row = 0; row < part.loaders.entries.size(); ++row)
        {
            const LoaderEntry& entry = part.loaders.entries[row];
            if (entry.loader_type >= loaders.size() || loaders[entry.loader_type].tag != kTag)
                continue;
            AnimalModelItem item;
            if (const auto it = names.find(entry.data); it != names.end())
                item.name = it->second;
            item.model = read_one(space, part, row);
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl::animal