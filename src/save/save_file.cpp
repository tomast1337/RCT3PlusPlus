// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/save_file.hpp"

#include <cstring>
#include <utility>

namespace rct3::save
{

    namespace
    {

        // Prefix of a headered file: two zero words, then the magic.
        constexpr std::uint32_t kMagic = 0xF11EDA2Au;
        constexpr std::uint32_t kHeaderBodySize = 0x40;
        constexpr std::size_t kPrefixSize = 16;
        constexpr std::uint32_t kMinVersion = 1;
        constexpr std::uint32_t kMaxVersion = 2;

        // Smallest encodings, used to reject counts that cannot fit in the remaining bytes before allocating.
        constexpr std::size_t kMinClassBytes = 2 + 4;         // name length, attribute count
        constexpr std::size_t kMinAttributeBytes = 2 + 2 + 8; // name length, type length, size, child count
        constexpr std::size_t kObjectPrefixBytes = 12;        // class index, id

        // Bounds-checked little-endian reader over a byte span.
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> data, std::size_t pos = 0)
                : data_(data)
                , pos_(pos)
            {
            }

            [[nodiscard]] std::size_t pos() const noexcept
            {
                return pos_;
            }
            [[nodiscard]] std::size_t remaining() const noexcept
            {
                return data_.size() - pos_;
            }

            bool u16(std::uint16_t& out)
            {
                return fixed(out);
            }
            bool u32(std::uint32_t& out)
            {
                return fixed(out);
            }
            bool u64(std::uint64_t& out)
            {
                return fixed(out);
            }

            bool skip(std::size_t n)
            {
                if (n > remaining())
                    return false;
                pos_ += n;
                return true;
            }

            bool text(std::size_t n, std::string_view& out)
            {
                if (n > remaining())
                    return false;
                out = std::string_view(reinterpret_cast<const char*>(data_.data() + pos_), n);
                pos_ += n;
                return true;
            }

        private:
            template<class T> bool fixed(T& out)
            {
                if (sizeof(T) > remaining())
                    return false;
                std::memcpy(&out, data_.data() + pos_, sizeof(T)); // host is little-endian on every supported target
                pos_ += sizeof(T);
                return true;
            }

            std::span<const std::byte> data_;
            std::size_t pos_;
        };

        std::expected<Attribute, Error> read_attribute(Reader& r, std::size_t depth)
        {
            if (depth > kMaxAttributeDepth)
                return std::unexpected(Error::BadClassTable);
            Attribute a;
            std::uint16_t name_len = 0;
            std::uint16_t type_len = 0;
            std::uint32_t child_count = 0;
            if (!r.u16(name_len) || !r.text(name_len, a.name))
                return std::unexpected(Error::Truncated);
            if (!r.u16(type_len) || !r.text(type_len, a.type))
                return std::unexpected(Error::Truncated);
            if (!r.u32(a.size) || !r.u32(child_count))
                return std::unexpected(Error::Truncated);
            if (child_count > r.remaining() / kMinAttributeBytes)
                return std::unexpected(Error::ImplausibleCount);
            a.children.reserve(child_count);
            for (std::uint32_t i = 0; i < child_count; ++i)
            {
                auto child = read_attribute(r, depth + 1);
                if (!child)
                    return std::unexpected(child.error());
                a.children.push_back(std::move(*child));
            }
            return a;
        }

        std::expected<std::vector<Class>, Error> read_class_table(Reader& r)
        {
            std::uint32_t count = 0;
            if (!r.u32(count))
                return std::unexpected(Error::Truncated);
            if (count > kMaxClasses || count > r.remaining() / kMinClassBytes)
                return std::unexpected(Error::ImplausibleCount);
            std::vector<Class> classes;
            classes.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                Class c;
                std::uint16_t name_len = 0;
                std::uint32_t attribute_count = 0;
                if (!r.u16(name_len) || !r.text(name_len, c.name))
                    return std::unexpected(Error::Truncated);
                if (!r.u32(attribute_count))
                    return std::unexpected(Error::Truncated);
                if (attribute_count > r.remaining() / kMinAttributeBytes)
                    return std::unexpected(Error::ImplausibleCount);
                c.attributes.reserve(attribute_count);
                for (std::uint32_t j = 0; j < attribute_count; ++j)
                {
                    auto a = read_attribute(r, 1);
                    if (!a)
                        return std::unexpected(a.error());
                    c.attributes.push_back(std::move(*a));
                }
                classes.push_back(std::move(c));
            }
            return classes;
        }

        // Advances over the attributes of one object. A fixed attribute is `size` bytes; a variable one is a u32
        // length followed by that many bytes. Compound attributes need no recursion: their nested layout only
        // describes the payload, which is skipped as a unit.
        bool skip_attributes(Reader& r, const Class& c)
        {
            for (const auto& a : c.attributes)
            {
                if (a.size != 0)
                {
                    if (!r.skip(a.size))
                        return false;
                }
                else
                {
                    std::uint32_t length = 0;
                    if (!r.u32(length) || !r.skip(length))
                        return false;
                }
            }
            return true;
        }

    } // namespace

    const char* to_string(Error e) noexcept
    {
        switch (e)
        {
            case Error::Truncated:
                return "truncated";
            case Error::BadHeader:
                return "bad header";
            case Error::UnsupportedVersion:
                return "unsupported version";
            case Error::ImplausibleCount:
                return "implausible count";
            case Error::BadClassTable:
                return "bad class table";
            case Error::BadClassIndex:
                return "bad class index";
            case Error::BadObjectTable:
                return "bad object table";
            case Error::TrailingData:
                return "trailing data";
            case Error::ChecksumMismatch:
                return "checksum mismatch";
        }
        return "unknown";
    }

    const char* to_string(SectionKind k) noexcept
    {
        switch (k)
        {
            case SectionKind::Header:
                return "header";
            case SectionKind::ClassTable:
                return "class table";
            case SectionKind::ObjectTable:
                return "object table";
            case SectionKind::Checksum:
                return "checksum";
        }
        return "unknown";
    }

    std::uint32_t compute_checksum(std::span<const std::byte> data) noexcept
    {
        // 16-bit state seeded with 55665; per byte: cipher = byte ^ (state >> 8), state = (cipher + state) *
        // 52845 + 22719 (mod 2^16). The cipher bytes are summed.
        std::uint16_t state = 55665;
        std::uint32_t sum = 0;
        for (const std::byte b : data)
        {
            const auto cipher = static_cast<std::uint8_t>(static_cast<std::uint8_t>(b) ^ (state >> 8));
            state = static_cast<std::uint16_t>((cipher + state) * 52845u + 22719u);
            sum += cipher;
        }
        return sum;
    }

    std::expected<SaveFile, Error> read_save(std::span<const std::byte> data, const ReadOptions& options)
    {
        SaveFile file;
        std::size_t body_start = 0;

        // A header is present when the first two words are zero and the third is the magic. Anything else is
        // treated as a headerless file whose class table starts at offset 0.
        if (data.size() >= kPrefixSize)
        {
            std::uint32_t prefix[4] = { };
            std::memcpy(prefix, data.data(), sizeof(prefix));
            if (prefix[0] == 0 && prefix[1] == 0 && prefix[2] == kMagic)
            {
                if (prefix[3] != kHeaderBodySize)
                    return std::unexpected(Error::BadHeader);
                if (data.size() < kHeaderSize + sizeof(std::uint32_t))
                    return std::unexpected(Error::Truncated);
                std::memcpy(file.header.words.data(), data.data() + kPrefixSize, sizeof(file.header.words));
                if (file.header.version() < kMinVersion || file.header.version() > kMaxVersion)
                    return std::unexpected(Error::UnsupportedVersion);
                file.has_header = true;
                body_start = kHeaderSize;
                file.sections.push_back({ SectionKind::Header, 0, kHeaderSize });
            }
        }

        // With a header, the last four bytes are the checksum and objects may not reach into them.
        const std::size_t body_end = file.has_header ? data.size() - sizeof(std::uint32_t) : data.size();
        const auto body = data.first(body_end);

        Reader r(body, body_start);
        auto classes = read_class_table(r);
        if (!classes)
            return std::unexpected(classes.error());
        file.classes = std::move(*classes);
        file.sections.push_back({ SectionKind::ClassTable, body_start, r.pos() - body_start });

        const std::size_t object_table_start = r.pos();
        std::uint32_t object_count = 0;
        if (!r.u32(object_count))
            return std::unexpected(Error::Truncated);
        if (object_count > r.remaining() / kObjectPrefixBytes)
            return std::unexpected(Error::ImplausibleCount);
        file.objects.reserve(object_count);
        for (std::uint32_t i = 0; i < object_count; ++i)
        {
            const std::size_t start = r.pos();
            Object o;
            if (!r.u32(o.class_index) || !r.u64(o.id))
                return std::unexpected(Error::BadObjectTable);
            if (o.class_index >= file.classes.size())
                return std::unexpected(Error::BadClassIndex);
            if (!skip_attributes(r, file.classes[o.class_index]))
                return std::unexpected(Error::BadObjectTable);
            o.offset = start;
            o.bytes = body.subspan(start, r.pos() - start);
            file.objects.push_back(o);
        }
        file.sections.push_back({ SectionKind::ObjectTable, object_table_start, r.pos() - object_table_start });

        if (r.pos() != body_end)
            return std::unexpected(Error::TrailingData);

        if (file.has_header)
        {
            file.has_checksum = true;
            std::memcpy(&file.stored_checksum, data.data() + body_end, sizeof(std::uint32_t));
            file.computed_checksum = compute_checksum(body);
            file.sections.push_back({ SectionKind::Checksum, body_end, sizeof(std::uint32_t) });
            if (options.verify_checksum && file.stored_checksum != file.computed_checksum)
                return std::unexpected(Error::ChecksumMismatch);
        }
        return file;
    }

} // namespace rct3::save
