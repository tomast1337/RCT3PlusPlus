// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park file's container, shared by the parts that write and read its chunks (see park_file.hpp), and
// the rule that writes a value of the park's models into a chunk field by field:
//
//   bool, integers and enumerations  little-endian, at their own size (an enumeration at its underlying type's)
//   floats                           IEEE 754, little-endian
//   std::string                      u32 byte length, then UTF-8
//   std::vector                      u32 count, then each element
//   std::array and glm vectors       each element
//   std::optional                    u8 1 then the value, or u8 0
//   std::variant                     u8 index of the alternative held, then its value (std::monostate: none)
//   a struct                         each member in the order declared; a unit (simulation/units.hpp) as its number
//
// A chunk written this way starts with a u64 hash of its type's layout (the kinds above, nested), so a reader
// whose models have changed since the file was written refuses the chunk rather than misread it.
// Internal to src/parkfile.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rct3::parkfile::detail
{

    inline constexpr std::array<char, 8> kMagic { 'R', '3', 'P', 'P', 'P', 'A', 'R', 'K' };
    inline constexpr std::size_t kChunkHeader = 16;

    // ----- writing -----

    class Writer
    {
    public:
        void bytes(const void* data, std::size_t size)
        {
            const auto* p = static_cast<const std::byte*>(data);
            out_.insert(out_.end(), p, p + size);
        }
        template<typename T> void number(T value)
        {
            if constexpr (std::is_floating_point_v<T>)
                number(std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(value));
            else
            {
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                bytes(&value, sizeof value);
            }
        }
        void text(std::string_view s)
        {
            number(static_cast<std::uint32_t>(s.size()));
            bytes(s.data(), s.size());
        }
        void vec3(glm::vec3 v)
        {
            number(v.x);
            number(v.y);
            number(v.z);
        }
        std::size_t size() const
        {
            return out_.size();
        }
        /// Overwrites the eight bytes at `at` with `value`.
        void patch(std::size_t at, std::uint64_t value)
        {
            if constexpr (std::endian::native == std::endian::big)
                value = std::byteswap(value);
            std::memcpy(out_.data() + at, &value, sizeof value);
        }
        std::vector<std::byte> take()
        {
            return std::move(out_);
        }

    private:
        std::vector<std::byte> out_;
    };

    // ----- reading -----

    /// Reads within a span; past its end, reads give zero and the reader is no longer ok.
    class Reader
    {
    public:
        explicit Reader(std::span<const std::byte> in)
            : in_(in)
        {
        }

        bool take(void* to, std::size_t size)
        {
            if (size > in_.size() - at_)
            {
                ok_ = false;
                at_ = in_.size();
                std::memset(to, 0, size);
                return false;
            }
            std::memcpy(to, in_.data() + at_, size);
            at_ += size;
            return true;
        }
        template<typename T> T number()
        {
            if constexpr (std::is_floating_point_v<T>)
                return std::bit_cast<T>(number<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>());
            else
            {
                T value { };
                take(&value, sizeof value);
                if constexpr (std::endian::native == std::endian::big)
                    value = std::byteswap(value);
                return value;
            }
        }
        std::string text()
        {
            const auto size = number<std::uint32_t>();
            if (size > left())
            {
                ok_ = false;
                at_ = in_.size();
                return { };
            }
            std::string s(size, '\0');
            take(s.data(), size);
            return s;
        }
        glm::vec3 vec3()
        {
            const float x = number<float>();
            const float y = number<float>();
            const float z = number<float>();
            return { x, y, z };
        }
        std::span<const std::byte> slice(std::size_t size)
        {
            if (size > left())
            {
                ok_ = false;
                at_ = in_.size();
                return { };
            }
            const auto s = in_.subspan(at_, size);
            at_ += size;
            return s;
        }
        std::size_t left() const
        {
            return in_.size() - at_;
        }
        bool ok() const
        {
            return ok_;
        }
        /// Marks what was read as wrong, as running out does.
        void fail()
        {
            ok_ = false;
            at_ = in_.size();
        }

    private:
        std::span<const std::byte> in_;
        std::size_t at_ = 0;
        bool ok_ = true;
    };

    /// Starts a chunk; returns where its length goes, filled in by end_chunk.
    inline std::size_t begin_chunk(Writer& w, std::string_view tag, std::uint32_t version)
    {
        w.bytes(tag.data(), 4);
        w.number(version);
        const std::size_t at = w.size();
        w.number(std::uint64_t { 0 });
        return at;
    }

    inline void end_chunk(Writer& w, std::size_t length_at)
    {
        w.patch(length_at, w.size() - length_at - 8);
    }

    /// The magic, the format version and the number of chunks to come.
    void write_header(Writer& w, std::uint32_t chunks);

    /// Checks the header and calls `chunk` with each chunk's tag, version and payload, in file order; stops at
    /// the first error, its own or the callback's. A chunk the callback leaves unread in part is an error.
    std::expected<void, std::string> read_chunks(std::span<const std::byte> bytes, const std::function<std::expected<void, std::string>(std::string_view tag, std::uint32_t version, Reader& body)>& chunk);

    // ----- values field by field -----

    template<typename T> struct IsVector : std::false_type
    {
    };
    template<typename T> struct IsVector<std::vector<T>> : std::true_type
    {
    };
    template<typename T> struct IsOptional : std::false_type
    {
    };
    template<typename T> struct IsOptional<std::optional<T>> : std::true_type
    {
    };
    template<typename T> struct IsArray : std::false_type
    {
    };
    template<typename T, std::size_t N> struct IsArray<std::array<T, N>> : std::true_type
    {
    };
    template<typename T> struct IsGlm : std::false_type
    {
    };
    template<glm::length_t L, typename T, glm::qualifier Q> struct IsGlm<glm::vec<L, T, Q>> : std::true_type
    {
    };

    template<typename T> struct IsVariant : std::false_type
    {
    };
    template<typename... T> struct IsVariant<std::variant<T...>> : std::true_type
    {
    };

    template<typename T>
    concept Scalar = std::is_arithmetic_v<T> || std::is_enum_v<T>;

    /// Writes the layout of `T` into `out`, as the hash is made from.
    template<typename T> void layout(std::string& out)
    {
        if constexpr (std::same_as<T, bool>)
            out += 'b';
        else if constexpr (std::is_enum_v<T>)
            out += std::format("e{}", sizeof(T));
        else if constexpr (std::is_floating_point_v<T>)
            out += std::format("f{}", sizeof(T));
        else if constexpr (std::is_integral_v<T>)
            out += std::format("{}{}", std::is_signed_v<T> ? 'i' : 'u', sizeof(T));
        else if constexpr (std::same_as<T, std::string>)
            out += 's';
        else if constexpr (IsVector<T>::value)
        {
            out += "v(";
            layout<typename T::value_type>(out);
            out += ')';
        }
        else if constexpr (IsOptional<T>::value)
        {
            out += "o(";
            layout<typename T::value_type>(out);
            out += ')';
        }
        else if constexpr (IsArray<T>::value)
        {
            out += std::format("a{}(", std::tuple_size_v<T>);
            layout<typename T::value_type>(out);
            out += ')';
        }
        else if constexpr (IsGlm<T>::value)
        {
            out += std::format("a{}(", T::length());
            layout<typename T::value_type>(out);
            out += ')';
        }
        else if constexpr (requires { T::kUnit; })
            layout<decltype(T::value)>(out); // A unit is written as its bare number, so wrapping one changes nothing
        else if constexpr (std::same_as<T, std::monostate>)
            out += 'n';
        else if constexpr (IsVariant<T>::value)
        {
            out += "x(";
            [&]<std::size_t... I>(std::index_sequence<I...>) { ((out += I > 0 ? "|" : "", layout<std::variant_alternative_t<I, T>>(out)), ...); }(std::make_index_sequence<std::variant_size_v<T>> { });
            out += ')';
        }
        else
        {
            static_assert(std::is_aggregate_v<T>, "a model value must be a struct of members this file can write");
            out += '{';
            const T value { };
            const auto& [... members] = value;
            (layout<std::remove_cvref_t<decltype(members)>>(out), ...);
            out += '}';
        }
    }

    /// FNV-1a over the layout of `T`.
    template<typename T> std::uint64_t layout_hash()
    {
        std::string text;
        layout<T>(text);
        std::uint64_t hash = 0xcbf29ce484222325;
        for (const char c : text)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3;
        }
        return hash;
    }

    template<typename T> void put(Writer& w, const T& value)
    {
        if constexpr (std::same_as<T, bool>)
            w.number(std::uint8_t { value });
        else if constexpr (std::is_enum_v<T>)
            w.number(std::to_underlying(value));
        else if constexpr (Scalar<T>)
            w.number(value);
        else if constexpr (std::same_as<T, std::string>)
            w.text(value);
        else if constexpr (IsVector<T>::value)
        {
            w.number(static_cast<std::uint32_t>(value.size()));
            for (const auto& item : value)
                put(w, item);
        }
        else if constexpr (IsOptional<T>::value)
        {
            w.number(std::uint8_t { value.has_value() });
            if (value)
                put(w, *value);
        }
        else if constexpr (IsArray<T>::value)
        {
            for (const auto& item : value)
                put(w, item);
        }
        else if constexpr (IsGlm<T>::value)
        {
            for (glm::length_t i = 0; i < T::length(); ++i)
                put(w, value[i]);
        }
        else if constexpr (std::same_as<T, std::monostate>)
            return;
        else if constexpr (IsVariant<T>::value)
        {
            static_assert(std::variant_size_v<T> <= 255);
            w.number(static_cast<std::uint8_t>(value.index()));
            std::visit([&](const auto& held) { put(w, held); }, value);
        }
        else
        {
            const auto& [... members] = value;
            // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): the analyzer loses track of structured binding packs
            (put(w, members), ...);
        }
    }

    /// Reads into `value`; a reader that runs out leaves the rest at zero and is no longer ok.
    template<typename T> void get(Reader& r, T& value)
    {
        if constexpr (std::same_as<T, bool>)
            value = r.number<std::uint8_t>() != 0;
        else if constexpr (std::is_enum_v<T>)
            value = static_cast<T>(r.number<std::underlying_type_t<T>>());
        else if constexpr (Scalar<T>)
            value = r.number<T>();
        else if constexpr (std::same_as<T, std::string>)
            value = r.text();
        else if constexpr (IsVector<T>::value)
        {
            // Every element takes at least a byte, so a count past the bytes left is not believed.
            const auto count = r.number<std::uint32_t>();
            if (count > r.left())
            {
                r.fail();
                return;
            }
            value.resize(count);
            for (auto& item : value)
            {
                get(r, item);
                if (!r.ok())
                    return;
            }
        }
        else if constexpr (IsOptional<T>::value)
        {
            value.reset();
            if (r.number<std::uint8_t>() != 0)
                get(r, value.emplace());
        }
        else if constexpr (IsArray<T>::value)
        {
            for (auto& item : value)
                get(r, item);
        }
        else if constexpr (IsGlm<T>::value)
        {
            for (glm::length_t i = 0; i < T::length(); ++i)
                get(r, value[i]);
        }
        else if constexpr (std::same_as<T, std::monostate>)
            return;
        else if constexpr (IsVariant<T>::value)
        {
            const auto index = r.number<std::uint8_t>();
            if (index >= std::variant_size_v<T>)
            {
                r.fail();
                return;
            }
            [&]<std::size_t... I>(std::index_sequence<I...>) { ((I == index ? get(r, value.template emplace<I>()) : void()), ...); }(std::make_index_sequence<std::variant_size_v<T>> { });
        }
        else
        {
            auto& [... members] = value;
            // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): the analyzer loses track of structured binding packs
            (get(r, members), ...);
        }
    }

    /// Writes `value` as the payload of chunk `tag`, after its layout hash.
    template<typename T> void put_chunk(Writer& w, std::string_view tag, std::uint32_t version, const T& value)
    {
        const std::size_t at = begin_chunk(w, tag, version);
        w.number(layout_hash<T>());
        put(w, value);
        end_chunk(w, at);
    }

    /// Reads a chunk put_chunk wrote.
    template<typename T> std::expected<void, std::string> get_chunk(std::string_view tag, Reader& r, T& value)
    {
        if (r.number<std::uint64_t>() != layout_hash<T>())
            return std::unexpected(std::format("{}: written by a build whose park model differs from this one's", tag));
        get(r, value);
        if (!r.ok())
            return std::unexpected(std::format("{}: ends early", tag));
        return { };
    }

} // namespace rct3::parkfile::detail
