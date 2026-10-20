// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font_images.hpp"

#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

#include <map>

namespace rct3::ui
{

    std::expected<std::vector<FontImages>, std::string> load_font_images(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        std::map<std::string, ovl::Texture, std::less<>> textures;
        std::vector<FontImages> fonts;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (item.texture)
                    textures.emplace(std::string(item.name), *item.texture);
            }
            for (ovl::FontItem& item : ovl::read_fonts(*overlay, *part))
            {
                if (!item.font)
                    continue;
                FontImages f { std::string(item.name), std::move(*item.font), { }, { } };
                fonts.push_back(std::move(f));
            }
        }

        std::vector<FontImages> out;
        for (FontImages& f : fonts)
        {
            const auto texture = textures.find(f.font.texture);
            if (texture == textures.end())
                continue;
            const std::size_t count = pixels.frame_count(texture->second);
            for (std::size_t i = 0; i < count; ++i)
                f.pages.push_back(pixels.frame(texture->second, i).value_or(ovl::RgbaImage { }));
            if (f.pages.empty())
                continue;
            f.face = std::string(f.font.face);
            f.font.face = { };
            f.font.texture = { };
            out.push_back(std::move(f));
        }
        return out;
    }

} // namespace rct3::ui
