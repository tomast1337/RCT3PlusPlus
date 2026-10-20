// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin_set.hpp"

#include "ovl/gui_skin.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "strings/ascii.hpp"

namespace rct3::ui
{

    std::expected<SkinSet, std::string> load_skin_set(const std::filesystem::path& path, std::span<const std::string_view> whole_textures)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        // Skins do not always spell their picture's name as the picture does.
        std::map<std::string, ovl::Texture, strings::LessIgnoringCase> textures;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (item.texture)
                    textures.emplace(item.name, *item.texture);
            }
        }

        SkinSet set;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::GuiSkinItem& item : ovl::read_gui_skins(*overlay, *part))
            {
                const auto layout = item.skin ? skin_layout(*item.skin) : std::nullopt;
                if (!layout)
                {
                    ++set.skipped;
                    continue;
                }
                const std::string texture(item.skin->texture);
                if (layout->x_count > 0 && !set.images.contains(texture))
                {
                    const auto source = textures.find(texture);
                    auto image = source == textures.end() ? std::nullopt : pixels.first_frame(source->second);
                    if (!image)
                    {
                        ++set.skipped;
                        continue;
                    }
                    set.images.emplace(texture, ovl::flipped_vertically(std::move(*image)));
                }
                set.skins.insert_or_assign(std::string(item.name), NamedSkin { *layout, texture });
            }
        }

        for (const std::string_view name : whole_textures)
        {
            const auto source = textures.find(name);
            if (source == textures.end() || set.skins.contains(name))
                continue;
            auto image = set.images.contains(name) ? std::nullopt : pixels.first_frame(source->second);
            if (image)
                set.images.emplace(std::string(name), ovl::flipped_vertically(std::move(*image)));
            const auto it = set.images.find(name);
            if (it == set.images.end())
            {
                ++set.skipped;
                continue;
            }
            const auto w = static_cast<float>(it->second.width);
            const auto h = static_cast<float>(it->second.height);
            set.skins.emplace(std::string(name), NamedSkin { { ovl::GuiSkinKind::Rect, { 0, w }, { 0, h }, 2, 2 }, std::string(name) });
        }
        return set;
    }

} // namespace rct3::ui
