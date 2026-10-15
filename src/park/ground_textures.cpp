// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_textures.hpp"

#include "ovl/flexi_texture.hpp"
#include "ovl/overlay.hpp"
#include "ovl/terrain_type.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "park/flexi_colours.hpp"
#include "strings/ascii.hpp"

#include <format>
#include <limits>
#include <vector>

namespace rct3::park
{

    namespace
    {
        constexpr std::string_view kCliffPrefix = "Cliff_";
        constexpr std::string_view kMaskEnding = ".alpha"; ///< Of a see-through mask's name, in any case
    } // namespace

    GroundPaint ground_paint(std::string_view terrain_type)
    {
        return terrain_type.starts_with(kCliffPrefix) ? GroundPaint::Cliff : GroundPaint::Surface;
    }

    std::string ground_key(GroundPaint paint, std::uint8_t index)
    {
        switch (paint)
        {
            case GroundPaint::Cliff:
                return "cliff " + std::to_string(index);
            case GroundPaint::PathA:
                return "path A " + std::to_string(index);
            case GroundPaint::PathB:
                return "path B " + std::to_string(index);
            case GroundPaint::Queue:
                return "queue " + std::to_string(index);
            case GroundPaint::Surface:
                break;
        }
        return "surface " + std::to_string(index);
    }

    std::string path_texture_file(std::string_view style)
    {
        return std::format("Path/{0}/{0}_Texture.common.ovl", style);
    }

    std::expected<std::size_t, std::string> add_path_images(GroundImages& images, const std::filesystem::path& path, std::string_view style, std::uint8_t index)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);
        std::size_t missing = 2;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                for (const GroundPaint paint : { GroundPaint::PathA, GroundPaint::PathB })
                {
                    if (!item.texture || item.name != std::format("Path_{}_Ground{}", style, paint == GroundPaint::PathA ? 'A' : 'B'))
                        continue;
                    if (auto image = pixels.first_frame(*item.texture))
                    {
                        images.insert_or_assign(ground_key(paint, index), std::move(*image));
                        --missing;
                    }
                }
            }
        }
        return missing;
    }

    std::string queue_texture_file(std::string_view style)
    {
        return std::format("Queue/{0}/{0}_Texture.common.ovl", style);
    }

    std::optional<std::size_t> queue_picture(std::span<const std::string_view> names, std::string_view style)
    {
        const std::string named = std::format("Ground{}", style);
        std::optional<std::size_t> chosen;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            const std::string_view n = names[i];
            if (n.size() >= kMaskEnding.size() && strings::equals_ignoring_case(n.substr(n.size() - kMaskEnding.size()), kMaskEnding))
                continue;
            if (n == named)
                return i;
            if (!chosen)
                chosen = i;
        }
        return chosen;
    }

    std::expected<bool, std::string> add_queue_image(GroundImages& images, const std::filesystem::path& path, const QueuePainting& painting, std::uint8_t index)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        std::vector<ovl::FlexiTextureItem> items;
        std::vector<std::string_view> names;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (ovl::FlexiTextureItem& item : ovl::read_flexi_textures(*overlay, *part))
            {
                if (!item.texture || item.texture->frames.empty())
                    continue;
                names.push_back(item.name);
                items.push_back(std::move(item));
            }
        }
        const auto at = queue_picture(names, painting.style);
        if (!at)
            return false;
        const ovl::FlexiTexture* chosen = &*items[*at].texture;
        const auto picture = palette_image(chosen->frames.front(), chosen->flags);
        if (!picture)
            return false;
        images.insert_or_assign(ground_key(GroundPaint::Queue, index), paint(*picture, flexi_colours(painting.colours)));
        return true;
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
