// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_textures.hpp"

#include "ovl/overlay.hpp"
#include "ovl/terrain_type.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

#include <limits>

namespace rct3::park
{

    namespace
    {
        constexpr std::string_view kCliffPrefix = "Cliff_";
    } // namespace

    GroundPaint ground_paint(std::string_view terrain_type)
    {
        return terrain_type.starts_with(kCliffPrefix) ? GroundPaint::Cliff : GroundPaint::Surface;
    }

    std::string ground_key(GroundPaint paint, std::uint8_t index)
    {
        return (paint == GroundPaint::Cliff ? "cliff " : "surface ") + std::to_string(index);
    }

    std::expected<std::size_t, std::string> add_ground_images(GroundImages& images, const std::filesystem::path& path)
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

        std::size_t missing = 0;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::TerrainTypeEntry& entry : ovl::read_terrain_types(*overlay, *part))
            {
                const auto source = entry.terrain ? textures.find(entry.terrain->texture) : textures.end();
                auto image = source == textures.end() ? std::nullopt : pixels.first_frame(source->second);
                if (!image || entry.terrain->index > std::numeric_limits<std::uint8_t>::max())
                {
                    ++missing;
                    continue;
                }
                images.insert_or_assign(ground_key(ground_paint(entry.name), static_cast<std::uint8_t>(entry.terrain->index)), std::move(*image));
            }
        }
        return missing;
    }

} // namespace rct3::park
