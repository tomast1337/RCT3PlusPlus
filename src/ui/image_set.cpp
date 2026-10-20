// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/image_set.hpp"

#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

namespace rct3::ui
{

    std::expected<ImageSet, std::string> load_image_set(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        ImageSet images;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (!item.texture)
                    continue;
                if (auto image = pixels.first_frame(*item.texture))
                    images.emplace(std::string(item.name), ovl::flipped_vertically(std::move(*image)));
            }
        }
        return images;
    }

} // namespace rct3::ui
