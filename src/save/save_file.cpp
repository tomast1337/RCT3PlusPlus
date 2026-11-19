// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/save_file.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace rct3::save
{

    namespace
    {

        // Prefix of a headered file: two zero words, then the magic, then the size of the header's body.
        constexpr u32 kMagic = 0xF11EDA2Au;
        constexpr u32 kHeaderBodySize = 0x40;
        // The short form, seen in some effect files: its own magic, then 1, then twelve words.
        constexpr u32 kShortMagic = 0xF11EDA1Au;
        constexpr u32 kShortMark = 1;
        constexpr usize kShortHeaderSize = 0x40;
        constexpr usize kPrefixSize = 16;
        constexpr u32 kMinVersion = 1;
        constexpr u32 kMaxVersion = 2;

        // Smallest encodings, used to reject counts that cannot fit in the remaining bytes before allocating.
        constexpr usize kMinClassBytes = 2 + 4;         // name length, attribute count
        constexpr usize kMinAttributeBytes = 2 + 2 + 8; // name length, type length, size, child count
        constexpr usize kObjectPrefixBytes = 12;        // class index, id

        // Bounds-checked little-endian reader over a byte span.
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> data, usize pos = 0)
                : data_(data)
                , pos_(pos)
            {
            }

            [[nodiscard]] usize pos() const noexcept
            {
                return pos_;
            }
            [[nodiscard]] usize remaining() const noexcept
            {
                return data_.size() - pos_;
            }

            bool read_u16(u16& out)
            {
                return fixed(out);
            }
            bool read_u32(u32& out)
            {
                return fixed(out);
            }
            bool read_u64(u64& out)
            {
                return fixed(out);
            }

            bool skip(usize n)
            {
                if (n > remaining())
                    return false;
                pos_ += n;
                return true;
            }

            bool text(usize n, std::string_view& out)
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
            usize pos_;
        };

        std::expected<Attribute, Error> read_attribute(Reader& r, usize depth)
        {
            if (depth > kMaxAttributeDepth)
                return std::unexpected(Error::BadClassTable);
            Attribute a;
            u16 name_len = 0;
            u16 type_len = 0;
            u32 child_count = 0;
            if (!r.read_u16(name_len) || !r.text(name_len, a.name))
                return std::unexpected(Error::Truncated);
            if (!r.read_u16(type_len) || !r.text(type_len, a.type))
                return std::unexpected(Error::Truncated);
            if (!r.read_u32(a.size) || !r.read_u32(child_count))
                return std::unexpected(Error::Truncated);
            if (child_count > r.remaining() / kMinAttributeBytes)
                return std::unexpected(Error::ImplausibleCount);
            a.children.reserve(child_count);
            for (u32 i = 0; i < child_count; ++i)
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
            u32 count = 0;
            if (!r.read_u32(count))
                return std::unexpected(Error::Truncated);
            if (count > kMaxClasses || count > r.remaining() / kMinClassBytes)
                return std::unexpected(Error::ImplausibleCount);
            std::vector<Class> classes;
            classes.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                Class c;
                u16 name_len = 0;
                u32 attribute_count = 0;
                if (!r.read_u16(name_len) || !r.text(name_len, c.name))
                    return std::unexpected(Error::Truncated);
                if (!r.read_u32(attribute_count))
                    return std::unexpected(Error::Truncated);
                if (attribute_count > r.remaining() / kMinAttributeBytes)
                    return std::unexpected(Error::ImplausibleCount);
                c.attributes.reserve(attribute_count);
                for (u32 j = 0; j < attribute_count; ++j)
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
                    u32 length = 0;
                    if (!r.read_u32(length) || !r.skip(length))
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
            case Error::ValueMismatch:
                return "value does not fit its attribute";
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

    u32 compute_checksum(std::span<const std::byte> data) noexcept
    {
        // 16-bit state seeded with 55665; per byte: cipher = byte ^ (state >> 8), state = (cipher + state) *
        // 52845 + 22719 (mod 2^16). The cipher bytes are summed.
        u16 state = 55665;
        u32 sum = 0;
        for (const std::byte b : data)
        {
            const auto cipher = static_cast<u8>(static_cast<u8>(b) ^ (state >> 8));
            state = static_cast<u16>((cipher + state) * 52845u + 22719u);
            sum += cipher;
        }
        return sum;
    }

    std::expected<SaveFile, Error> read_save(std::span<const std::byte> data, const ReadOptions& options)
    {
        SaveFile file;
        usize body_start = 0;

        // A header is present when the first two words are zero and the third is the magic. Anything else is
        // treated as a headerless file whose class table starts at offset 0.
        if (data.size() >= kPrefixSize)
        {
            u32 prefix[4] = { };
            std::memcpy(prefix, data.data(), sizeof(prefix));
            const bool full = prefix[2] == kMagic;
            if (prefix[0] == 0 && prefix[1] == 0 && (full || prefix[2] == kShortMagic))
            {
                if (prefix[3] != (full ? kHeaderBodySize : kShortMark))
                    return std::unexpected(Error::BadHeader);
                const usize size = full ? kHeaderSize : kShortHeaderSize;
                if (data.size() < size + sizeof(u32))
                    return std::unexpected(Error::Truncated);
                std::memcpy(file.header.words.data(), data.data() + kPrefixSize, size - kPrefixSize);
                if (file.header.version() < kMinVersion || file.header.version() > kMaxVersion)
                    return std::unexpected(Error::UnsupportedVersion);
                file.has_header = true;
                body_start = size;
                file.sections.push_back({ SectionKind::Header, 0, size });
            }
        }

        // With a header, the last four bytes are the checksum and objects may not reach into them.
        const usize body_end = file.has_header ? data.size() - sizeof(u32) : data.size();
        const auto body = data.first(body_end);

        Reader r(body, body_start);
        auto classes = read_class_table(r);
        if (!classes)
            return std::unexpected(classes.error());
        file.classes = std::move(*classes);
        file.sections.push_back({ SectionKind::ClassTable, body_start, r.pos() - body_start });

        const usize object_table_start = r.pos();
        u32 object_count = 0;
        if (!r.read_u32(object_count))
            return std::unexpected(Error::Truncated);
        if (object_count > r.remaining() / kObjectPrefixBytes)
            return std::unexpected(Error::ImplausibleCount);
        file.objects.reserve(object_count);
        for (u32 i = 0; i < object_count; ++i)
        {
            const usize start = r.pos();
            Object o;
            if (!r.read_u32(o.class_index) || !r.read_u64(o.id))
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
            std::memcpy(&file.stored_checksum, data.data() + body_end, sizeof(u32));
            file.computed_checksum = compute_checksum(body);
            file.sections.push_back({ SectionKind::Checksum, body_end, sizeof(u32) });
            if (options.verify_checksum && file.stored_checksum != file.computed_checksum)
                return std::unexpected(Error::ChecksumMismatch);
        }
        return file;
    }

    std::expected<std::vector<std::byte>, Error> write_save(const std::optional<Header>& header, std::span<const Class> classes, std::span<const ObjectRecord> objects)
    {
        std::vector<std::byte> out;
        const auto put = [&out]<class T>(T v) {
            const auto at = out.size();
            out.resize(at + sizeof v);
            std::memcpy(out.data() + at, &v, sizeof v);
        };
        const auto text = [&](std::string_view s) {
            if (s.size() > UINT16_MAX)
                return false;
            put(static_cast<u16>(s.size()));
            const auto at = out.size();
            out.resize(at + s.size());
            std::memcpy(out.data() + at, s.data(), s.size());
            return true;
        };
        const auto attribute = [&](this const auto& self, const Attribute& a) -> bool {
            if (!text(a.name) || !text(a.type))
                return false;
            put(a.size);
            put(static_cast<u32>(a.children.size()));
            return std::ranges::all_of(a.children, [&](const Attribute& child) { return self(child); });
        };

        if (header)
        {
            for (const u32 w : { 0u, 0u, kMagic, kHeaderBodySize })
                put(w);
            for (const u32 w : header->words)
                put(w);
        }
        if (classes.size() > kMaxClasses)
            return std::unexpected(Error::ImplausibleCount);
        put(static_cast<u32>(classes.size()));
        for (const Class& c : classes)
        {
            if (!text(c.name))
                return std::unexpected(Error::ValueMismatch);
            put(static_cast<u32>(c.attributes.size()));
            if (!std::ranges::all_of(c.attributes, attribute))
                return std::unexpected(Error::ValueMismatch);
        }
        put(static_cast<u32>(objects.size()));
        for (const ObjectRecord& o : objects)
        {
            if (o.class_index >= classes.size())
                return std::unexpected(Error::BadClassIndex);
            // Checked the way the reader frames it, so a payload that would read back as another object's bytes
            // is refused here rather than written.
            Reader r(o.payload);
            if (!skip_attributes(r, classes[o.class_index]) || r.remaining() != 0)
                return std::unexpected(Error::BadObjectTable);
            put(o.class_index);
            put(o.id);
            out.insert(out.end(), o.payload.begin(), o.payload.end());
        }
        if (header)
            put(compute_checksum(out));
        return out;
    }

} // namespace rct3::save
