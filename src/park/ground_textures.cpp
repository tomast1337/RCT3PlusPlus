// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_textures.hpp"

#include "ovl/flexi_texture.hpp"
#include "ovl/overlay.hpp"
#include "ovl/terrain_type.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "park/flexi_colours.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <ranges>
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
            case GroundPaint::TunnelWall:
                return "tunnel wall " + std::to_string(index);
            case GroundPaint::TunnelCeiling:
                return "tunnel ceiling " + std::to_string(index);
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
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
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

    std::expected<bool, std::string> add_named_image(GroundImages& images, const std::filesystem::path& path, std::string_view name, std::string_view key)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (!item.texture || item.name != name)
                    continue;
                if (auto image = pixels.first_frame(*item.texture))
                {
                    images.insert_or_assign(std::string(key), std::move(*image));
                    return true;
                }
            }
        }
        return false;
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

    void apply_queue_mask(ovl::RgbaImage& picture, const PaletteImage& mask)
    {
        if (mask.width != picture.width || mask.height != picture.height || mask.indices.size() * 4 != picture.pixels.size())
            return;
        for (std::size_t i = 0; i < mask.indices.size(); ++i)
            picture.pixels[i * 4 + 3] = static_cast<std::uint8_t>(picture.pixels[i * 4 + 3] * mask.palette[mask.indices[i]].r / 255);
    }

    std::expected<bool, std::string> add_queue_image(GroundImages& images, const std::filesystem::path& path, const QueuePainting& painting, std::uint8_t index)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        std::vector<ovl::FlexiTextureItem> items;
        std::vector<std::string_view> names;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
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
        ovl::RgbaImage painted = paint(*picture, flexi_colours(painting.colours));
        const std::string mask_name = std::string(names[*at]) + std::string(kMaskEnding);
        const auto mask = std::ranges::find_if(names, [&](std::string_view n) { return strings::equals_ignoring_case(n, mask_name); });
        if (mask != names.end())
        {
            const ovl::FlexiTexture& texture = *items[static_cast<std::size_t>(mask - names.begin())].texture;
            if (const auto grey = palette_image(texture.frames.front(), 0))
                apply_queue_mask(painted, *grey);
        }
        images.insert_or_assign(ground_key(GroundPaint::Queue, index), std::move(painted));
        return true;
    }

    std::string tunnel_texture_file(std::string_view style)
    {
        return std::format("Style/Vanilla/TerrainSurfaces/{0}/{0}_TunnelTextures.common.ovl", style);
    }

    namespace
    {
        ovl::RgbaImage cut_out(const ovl::RgbaImage& from, const PictureRegion& region)
        {
            const auto at = [&](float share, std::uint32_t side) { return std::min(side, static_cast<std::uint32_t>(share * static_cast<float>(side))); };
            const std::uint32_t left = at(region.left, from.width);
            const std::uint32_t top = at(region.top, from.height);
            ovl::RgbaImage out;
            out.width = std::max(at(region.right, from.width), left + 1) - left;
            out.height = std::max(at(region.bottom, from.height), top + 1) - top;
            out.pixels.reserve(std::size_t { out.width } * out.height * 4);
            for (std::uint32_t y = 0; y < out.height; ++y)
            {
                const std::size_t row = (std::size_t { std::min(top + y, from.height - 1) } * from.width + left) * 4;
                out.pixels.insert(out.pixels.end(), from.pixels.begin() + static_cast<std::ptrdiff_t>(row), from.pixels.begin() + static_cast<std::ptrdiff_t>(row + std::size_t { out.width } * 4));
            }
            return out;
        }

        std::array<float, 3> mean_colour(const ovl::RgbaImage& image)
        {
            std::array<double, 3> sum { };
            for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4)
                for (std::size_t c = 0; c < 3; ++c)
                    sum[c] += image.pixels[i + c];
            const double count = std::max<double>(1, static_cast<double>(image.pixels.size() / 4));
            return { static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count), static_cast<float>(sum[2] / count) };
        }
    } // namespace

    std::expected<std::optional<TunnelPictures>, std::string> load_tunnel_pictures(const std::filesystem::path& path, const TunnelStyle& style)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::FlexiTextureItem& item : ovl::read_flexi_textures(*overlay, *part))
            {
                if (!item.texture || item.texture->frames.empty())
                    continue;
                const auto picture = ovl::decode_flexi_frame(item.texture->frames.front());
                if (!picture)
                    continue;
                return TunnelPictures { cut_out(*picture, style.wall), cut_out(*picture, style.ceiling) };
            }
        }
        return std::nullopt;
    }

    void add_tunnel_images(GroundImages& images, std::span<const TunnelPictures> styles)
    {
        if (styles.empty())
            return;
        std::vector<std::array<float, 3>> colours;
        for (const TunnelPictures& s : styles)
            colours.push_back(mean_colour(s.wall));
        for (int cliff = 0; cliff <= std::numeric_limits<std::uint8_t>::max(); ++cliff)
        {
            const auto face = images.find(ground_key(GroundPaint::Cliff, static_cast<std::uint8_t>(cliff)));
            if (face == images.end())
                continue;
            const auto colour = mean_colour(face->second);
            const auto distance = [&](std::size_t s) {
                float d = 0;
                for (std::size_t c = 0; c < 3; ++c)
                    d += (colours[s][c] - colour[c]) * (colours[s][c] - colour[c]);
                return d;
            };
            const std::size_t nearest = std::ranges::min(std::views::iota(std::size_t { 0 }, styles.size()), { }, distance);
            images.insert_or_assign(ground_key(GroundPaint::TunnelWall, static_cast<std::uint8_t>(cliff)), styles[nearest].wall);
            images.insert_or_assign(ground_key(GroundPaint::TunnelCeiling, static_cast<std::uint8_t>(cliff)), styles[nearest].ceiling);
        }
    }

    std::expected<std::size_t, std::string> add_ground_images(GroundImages& images, const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        std::map<std::string, ovl::Texture, std::less<>> textures;
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

        std::size_t missing = 0;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
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

    std::expected<std::vector<GroundSurface>, std::string> ground_surfaces(const std::filesystem::path& path, GroundPaint paint)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        std::vector<GroundSurface> out;
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TerrainTypeEntry& entry : ovl::read_terrain_types(*overlay, *part))
            {
                if (entry.terrain && ground_paint(entry.name) == paint && entry.terrain->index <= std::numeric_limits<std::uint8_t>::max())
                    out.push_back({ static_cast<std::uint8_t>(entry.terrain->index), std::string(entry.terrain->label_name), std::string(entry.terrain->help_name) });
            }
        }
        return out;
    }

} // namespace rct3::park
