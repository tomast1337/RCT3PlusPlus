// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_model.hpp"

#include <cstring>
#include <span>
#include <unordered_map>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "mdl";
        constexpr std::size_t kHeaderSize = 0x50;
        constexpr std::uint32_t kMaxBones = 256; ///< Vertices name bones by one byte
        constexpr std::uint32_t kMaxCount = 1u << 16;

        // Header fields.
        constexpr std::size_t kFlags = 0x00;
        constexpr std::size_t kLevelCount = 0x04;    // u8
        constexpr std::size_t kStoredLevels = 0x05;  // u8: levels kept in the first chunk
        constexpr std::size_t kMaterialCount = 0x06; // u16
        constexpr std::size_t kBoundsMin = 0x14;
        constexpr std::size_t kBoundsMax = 0x20;
        constexpr std::size_t kPrefixWords = 0x4C; // u32: extra words after the chunk's counts
        /// Flags of layouts not seen in any file: levels kept elsewhere and other name lists.
        constexpr std::uint32_t kUnsupportedFlags = 0x180000;

        constexpr std::size_t kBoneSize = 96;           // offset (4 floats), turn (4 floats), inverse rest matrix (16)
        constexpr std::size_t kLevelSize = 20;          // two u16 counts, bone count, three pointers
        constexpr std::size_t kMeshSize = 32;           // format, index count, index sets, vertex count, then runtime fields
        constexpr std::size_t kGroupSize = 16;          // material, two list lengths, then pointers
        constexpr std::uint32_t kVertexFormat = 0x1305; // position, normal, four bones and weights, colour, one uv

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
            std::size_t at() const noexcept
            {
                return at_;
            }
            void align() noexcept
            {
                at_ = (at_ + 15) & ~std::size_t { 15 };
                check(0);
            }
            std::span<const std::byte> take(std::size_t size) noexcept
            {
                if (!check(size))
                    return { };
                const auto out = bytes_.subspan(at_, size);
                at_ += size;
                return out;
            }
            std::uint32_t u32() noexcept
            {
                return word<std::uint32_t>();
            }
            std::uint16_t u16() noexcept
            {
                return word<std::uint16_t>();
            }

        private:
            bool check(std::size_t size) noexcept
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
            std::size_t at_ = 0;
            bool ok_ = true;
        };

        float f32_at(std::span<const std::byte> b, std::size_t at)
        {
            float v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::span<const std::byte> chunk_bytes(const Part& part, const Chunk& chunk)
        {
            if (chunk.file_offset > part.bytes.size() || part.bytes.size() - chunk.file_offset < chunk.size)
                return { };
            return std::span(part.bytes).subspan(chunk.file_offset, chunk.size);
        }

        std::optional<AnimalModel> read_one(const AddressSpace& space, const Part& part, std::size_t row)
        {
            if (row >= part.loaders.extra.size() || part.loaders.extra[row].size() != 2)
                return std::nullopt;
            const auto header = space.bytes_at(part.loaders.entries[row].data, kHeaderSize);
            if (!header)
                return std::nullopt;
            std::uint32_t flags = 0;
            std::memcpy(&flags, header->data() + kFlags, sizeof flags);
            const auto level_count = std::to_integer<std::uint32_t>((*header)[kLevelCount]);
            const auto stored_levels = std::to_integer<std::uint32_t>((*header)[kStoredLevels]);
            std::uint16_t material_count = 0;
            std::memcpy(&material_count, header->data() + kMaterialCount, sizeof material_count);
            std::uint32_t prefix_words = 0;
            std::memcpy(&prefix_words, header->data() + kPrefixWords, sizeof prefix_words);
            if ((flags & kUnsupportedFlags) != 0 || stored_levels != level_count || prefix_words > kMaxCount)
                return std::nullopt;

            AnimalModel model;
            for (std::size_t k = 0; k < 3; ++k)
            {
                model.bounds_min[k] = f32_at(*header, kBoundsMin + k * 4);
                model.bounds_max[k] = f32_at(*header, kBoundsMax + k * 4);
            }

            const auto data = chunk_bytes(part, part.loaders.extra[row][0]);
            const auto names = chunk_bytes(part, part.loaders.extra[row][1]);
            Cursor c(data);
            const std::uint32_t bone_count = c.u32();
            c.u32(); // Lengths of two lookup tables the game builds itself.
            const std::uint32_t extra_links = c.u32();
            c.u32();
            if (!c.ok() || bone_count > kMaxBones || extra_links != 0)
                return std::nullopt;
            c.take(std::size_t { prefix_words } * 4);
            c.align();

            // Bones, then a parent and a sibling link for each.
            const auto bones = c.take(std::size_t { bone_count } * kBoneSize);
            const auto links = c.take(std::size_t { bone_count } * 4);
            if (!c.ok())
                return std::nullopt;
            std::string_view name_text(reinterpret_cast<const char*>(names.data()), names.size());
            model.bones.reserve(bone_count);
            for (std::uint32_t i = 0; i < bone_count; ++i)
            {
                AnimalBone bone;
                const std::size_t end = name_text.find('\0');
                if (end == std::string_view::npos)
                    return std::nullopt;
                bone.name = name_text.substr(0, end);
                name_text.remove_prefix(end + 1);
                std::int16_t parent = 0;
                std::memcpy(&parent, links.data() + std::size_t { i } * 4, sizeof parent);
                if (parent < -1 || parent >= static_cast<std::int32_t>(i))
                    return std::nullopt;
                bone.parent = parent;
                const auto raw = bones.subspan(std::size_t { i } * kBoneSize, kBoneSize);
                for (std::size_t k = 0; k < 3; ++k)
                    bone.position[k] = f32_at(raw, k * 4);
                for (std::size_t k = 0; k < 4; ++k)
                    bone.rotation[k] = f32_at(raw, 16 + k * 4);
                model.bones.push_back(bone);
            }
            c.align();

            // Material names: their lengths, then the names.
            std::vector<std::uint32_t> lengths(material_count);
            for (std::uint32_t& length : lengths)
                length = c.u32();
            std::vector<std::string_view> materials;
            for (const std::uint32_t length : lengths)
            {
                const auto raw = c.take(length);
                if (!c.ok())
                    return std::nullopt;
                const std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
                materials.push_back(text.substr(0, text.find('\0')));
            }
            c.align();

            // Level records, then a word per level for an extra record that no file has.
            const auto levels = c.take(std::size_t { level_count } * kLevelSize);
            for (std::uint32_t i = 0; i < level_count; ++i)
            {
                if (c.u32() != 0)
                    return std::nullopt;
            }
            c.align();
            if (!c.ok())
                return std::nullopt;

            for (std::uint32_t l = 0; l < level_count; ++l)
            {
                const auto record = levels.subspan(std::size_t { l } * kLevelSize, kLevelSize);
                std::uint16_t group_count = 0;
                std::uint16_t mesh_count = 0;
                std::uint32_t bone_mask = 0;
                std::memcpy(&group_count, record.data(), sizeof group_count);
                std::memcpy(&mesh_count, record.data() + 2, sizeof mesh_count);
                std::memcpy(&bone_mask, record.data() + 16, sizeof bone_mask);
                // A level that marks which bones it uses keeps a bit per bone first.
                if (bone_mask != 0)
                    c.take((std::size_t { bone_count } + 31) / 32 * 4);
                const auto meshes = c.take(std::size_t { mesh_count } * kMeshSize);
                c.align();
                if (!c.ok())
                    return std::nullopt;
                std::vector<BoneMesh> level;
                for (std::uint16_t m = 0; m < mesh_count; ++m)
                {
                    const auto raw = meshes.subspan(std::size_t { m } * kMeshSize, kMeshSize);
                    std::uint32_t format = 0;
                    std::uint32_t index_count = 0;
                    std::uint16_t index_sets = 0;
                    std::uint16_t vertex_count = 0;
                    std::memcpy(&format, raw.data(), sizeof format);
                    std::memcpy(&index_count, raw.data() + 4, sizeof index_count);
                    std::memcpy(&index_sets, raw.data() + 8, sizeof index_sets);
                    std::memcpy(&vertex_count, raw.data() + 10, sizeof vertex_count);
                    if (format != kVertexFormat || index_sets != 1 || index_count > kMaxCount * 3 || index_count % 3 != 0)
                        return std::nullopt;
                    BoneMesh mesh;
                    mesh.vertex_count = vertex_count;
                    mesh.index_count = index_count;
                    mesh.vertices = c.take(std::size_t { vertex_count } * kBoneVertexSize);
                    mesh.indices = c.take(std::size_t { index_count } * 2);
                    c.take((4 - c.at() % 4) % 4);
                    if (!c.ok())
                        return std::nullopt;
                    for (std::uint32_t i = 0; i < index_count; ++i)
                    {
                        if (mesh.index(i) >= vertex_count)
                            return std::nullopt;
                    }
                    for (std::uint32_t v = 0; v < vertex_count; ++v)
                    {
                        const BoneVertex vertex = mesh.vertex(v);
                        for (std::size_t k = 0; k < 4; ++k)
                            if (vertex.weights[k] != 0 && vertex.bones[k] >= bone_count)
                                return std::nullopt;
                    }
                    level.push_back(mesh);
                }
                // Groups: each names a material and the level's meshes painted with it.
                const auto groups = c.take(std::size_t { group_count } * kGroupSize);
                for (std::uint16_t g = 0; g < group_count && c.ok(); ++g)
                {
                    const auto raw = groups.subspan(std::size_t { g } * kGroupSize, kGroupSize);
                    std::uint16_t material = 0;
                    std::uint16_t listed = 0;
                    std::uint16_t others = 0;
                    std::memcpy(&material, raw.data(), sizeof material);
                    std::memcpy(&listed, raw.data() + 2, sizeof listed);
                    std::memcpy(&others, raw.data() + 4, sizeof others);
                    if (material >= materials.size())
                        return std::nullopt;
                    for (std::uint16_t k = 0; k < listed; ++k)
                    {
                        const std::uint16_t mesh = static_cast<std::uint16_t>(c.u32() & 0xFFFF);
                        if (!c.ok() || mesh >= level.size())
                            return std::nullopt;
                        level[mesh].texture = materials[material];
                    }
                    c.take(std::size_t { others } * 4);
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

        std::unordered_map<std::uint32_t, std::string_view> names;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kTag)
                names.emplace(symbol.data, parts.name);
        }

        std::vector<AnimalModelItem> items;
        for (std::size_t row = 0; row < part.loaders.entries.size(); ++row)
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

} // namespace rct3::ovl
