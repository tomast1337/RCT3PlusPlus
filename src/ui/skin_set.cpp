// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin_set.hpp"

#include "ovl/gui_skin.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

namespace rct3::ui
{

    std::expected<SkinSet, std::string> load_skin_set(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        std::map<std::string, ovl::Texture, std::less<>> textures;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (item.texture)
                    textures.emplace(item.name, *item.texture);
            }
        }

        SkinSet set;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
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
        return set;
    }

} // namespace rct3::ui
