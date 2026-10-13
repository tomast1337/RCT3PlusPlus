// SPDX-License-Identifier: GPL-3.0-or-later
#include "image/picture_file.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <fstream>
#include <memory>
#include <stb/stb_image.h>

namespace rct3::image
{

    namespace
    {

        struct FreeImage
        {
            void operator()(stbi_uc* pixels) const
            {
                stbi_image_free(pixels);
            }
        };

    } // namespace

    std::expected<ovl::RgbaImage, std::string> decode_picture(std::span<const std::byte> data)
    {
        if (data.empty() || data.size() > static_cast<std::size_t>(INT_MAX))
            return std::unexpected("not a picture");
        const auto* bytes = reinterpret_cast<const stbi_uc*>(data.data());
        const int length = static_cast<int>(data.size());
        int width = 0;
        int height = 0;
        int channels = 0;
        // Checked before decoding, so a huge size in a header costs nothing.
        if (stbi_info_from_memory(bytes, length, &width, &height, &channels) == 0)
            return std::unexpected(std::string("not a picture: ") + stbi_failure_reason());
        if (width <= 0 || height <= 0 || static_cast<std::uint32_t>(width) > ovl::kMaxImageSide || static_cast<std::uint32_t>(height) > ovl::kMaxImageSide)
            return std::unexpected("picture of " + std::to_string(width) + "x" + std::to_string(height) + " is too large");
        const std::unique_ptr<stbi_uc, FreeImage> pixels(stbi_load_from_memory(bytes, length, &width, &height, &channels, 4));
        if (!pixels)
            return std::unexpected(std::string("cannot decode the picture: ") + stbi_failure_reason());
        ovl::RgbaImage image;
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        std::memcpy(image.pixels.data(), pixels.get(), image.pixels.size());
        return image;
    }

    std::expected<ovl::RgbaImage, std::string> load_picture(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::unexpected("cannot read " + path.string());
        std::vector<std::byte> data;
        std::array<char, 1 << 16> buffer { };
        while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0)
        {
            const auto* begin = reinterpret_cast<const std::byte*>(buffer.data());
            data.insert(data.end(), begin, begin + in.gcount());
        }
        auto image = decode_picture(data);
        if (!image)
            return std::unexpected(path.string() + ": " + image.error());
        return image;
    }

    bool is_picture_file(const std::filesystem::path& path)
    {
        const std::string extension = path.extension().string();
        for (const char* known : { ".jpg", ".jpeg", ".png", ".bmp", ".tga" })
        {
            if (strings::equals_ignoring_case(extension, known))
                return true;
        }
        return false;
    }

    std::vector<std::filesystem::path> list_pictures(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->is_regular_file(ec) && is_picture_file(it->path()))
                out.push_back(it->path());
        }
        std::ranges::sort(out);
        return out;
    }

} // namespace rct3::image
