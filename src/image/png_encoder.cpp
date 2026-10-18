// SPDX-License-Identifier: GPL-3.0-or-later
#include "image/png_encoder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>

namespace rct3::image
{

    namespace
    {

        constexpr std::array<std::uint32_t, 256> kCrcTable = [] {
            std::array<std::uint32_t, 256> table { };
            for (std::uint32_t n = 0; n < 256; ++n)
            {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
                table[n] = c;
            }
            return table;
        }();

        std::uint32_t crc32(std::span<const std::byte> bytes)
        {
            std::uint32_t c = 0xffffffffu;
            for (const std::byte b : bytes)
                c = kCrcTable[(c ^ std::to_integer<std::uint32_t>(b)) & 0xff] ^ (c >> 8);
            return c ^ 0xffffffffu;
        }

        void put_u32(std::vector<std::byte>& out, std::uint32_t v)
        {
            for (int shift = 24; shift >= 0; shift -= 8)
                out.push_back(static_cast<std::byte>((v >> shift) & 0xff));
        }

        void put_chunk(std::vector<std::byte>& out, std::string_view type, std::span<const std::byte> data)
        {
            put_u32(out, static_cast<std::uint32_t>(data.size()));
            const std::size_t start = out.size();
            for (const char c : type)
                out.push_back(static_cast<std::byte>(c));
            out.insert(out.end(), data.begin(), data.end());
            put_u32(out, crc32(std::span(out).subspan(start)));
        }

        // A zlib stream of stored (uncompressed) deflate blocks.
        std::vector<std::byte> stored_zlib(std::span<const std::byte> raw)
        {
            constexpr std::size_t kBlock = 65535; // The most a stored block holds
            std::vector<std::byte> out { std::byte { 0x78 }, std::byte { 0x01 } };
            std::size_t at = 0;
            do
            {
                const std::size_t n = std::min(kBlock, raw.size() - at);
                const bool last = at + n == raw.size();
                out.push_back(std::byte { last ? std::uint8_t { 1 } : std::uint8_t { 0 } });
                const auto len = static_cast<std::uint16_t>(n);
                const auto nlen = static_cast<std::uint16_t>(~len);
                for (const std::uint16_t v : { len, nlen })
                {
                    out.push_back(static_cast<std::byte>(v & 0xff));
                    out.push_back(static_cast<std::byte>(v >> 8));
                }
                out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
                at += n;
            } while (at < raw.size());

            std::uint32_t a = 1;
            std::uint32_t b = 0;
            for (const std::byte v : raw)
            {
                a = (a + std::to_integer<std::uint32_t>(v)) % 65521;
                b = (b + a) % 65521;
            }
            put_u32(out, (b << 16) | a);
            return out;
        }

    } // namespace

    std::vector<std::byte> encode_png(const ovl::RgbaImage& image)
    {
        const std::size_t row = std::size_t { image.width } * 4;
        if (image.width == 0 || image.height == 0 || image.pixels.size() < row * image.height)
            return { };

        // Each row starts with its filter type, 0 (none).
        std::vector<std::byte> raw;
        raw.reserve((row + 1) * image.height);
        for (std::uint32_t y = 0; y < image.height; ++y)
        {
            raw.push_back(std::byte { 0 });
            const auto* first = image.pixels.data() + row * y;
            for (std::size_t i = 0; i < row; ++i)
                raw.push_back(static_cast<std::byte>(first[i]));
        }

        std::vector<std::byte> out;
        for (const std::uint8_t v : std::to_array<std::uint8_t>({ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' }))
            out.push_back(static_cast<std::byte>(v));
        std::vector<std::byte> header;
        put_u32(header, image.width);
        put_u32(header, image.height);
        // 8 bits per channel, colour type 6 (RGBA), deflate, adaptive filtering, no interlace.
        for (const std::uint8_t v : std::to_array<std::uint8_t>({ 8, 6, 0, 0, 0 }))
            header.push_back(static_cast<std::byte>(v));
        put_chunk(out, "IHDR", header);
        put_chunk(out, "IDAT", stored_zlib(raw));
        put_chunk(out, "IEND", { });
        return out;
    }

    std::expected<void, std::string> save_png(const std::filesystem::path& path, const ovl::RgbaImage& image)
    {
        const std::vector<std::byte> bytes = encode_png(image);
        if (bytes.empty())
            return std::unexpected("empty image");
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
            return std::unexpected("cannot write " + path.string());
        return { };
    }

    std::expected<std::filesystem::path, std::string> save_numbered_png(const std::filesystem::path& folder, std::string_view named, const ovl::RgbaImage& image)
    {
        // Prefixes carry names players type, so nothing in them may lead out of the folder or upset a file system.
        std::string prefix { named };
        for (char& c : prefix)
            if (static_cast<unsigned char>(c) < 0x80 && !std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '_')
                c = '_';
        const std::vector<std::byte> bytes = encode_png(image);
        if (bytes.empty())
            return std::unexpected("empty image");
        std::uint32_t highest = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        {
            const std::string name = entry.path().filename().string();
            if (!name.starts_with(prefix) || !name.ends_with(".png"))
                continue;
            const std::string_view digits = std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - 4);
            std::uint32_t n = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
            if (error == std::errc { } && end == digits.data() + digits.size() && !digits.empty())
                highest = std::max(highest, n);
        }
        if (ec)
            return std::unexpected("cannot read " + folder.string() + ": " + ec.message());
        // A file made since the folder was read is passed over rather than replaced.
        constexpr int kTries = 16;
        for (std::uint32_t n = highest + 1; n <= highest + kTries; ++n)
        {
            std::filesystem::path path = folder / std::format("{}{:04}.png", prefix, n);
            std::ofstream file(path, std::ios::binary | std::ios::noreplace);
            if (!file.is_open())
                continue;
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return std::unexpected("cannot write " + path.string());
            return path;
        }
        return std::unexpected("cannot make a new file in " + folder.string());
    }

} // namespace rct3::image
