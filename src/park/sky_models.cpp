// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_models.hpp"

#include "ovl/overlay.hpp"
#include "ovl/static_shape.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::park
{

    namespace
    {

        /// Each part of the sky: its shape and the picture it is drawn with.
        struct SkyPart
        {
            SkyShape SkyModels::* slot;
            std::string_view shape;
            std::string_view picture;
        };

        constexpr std::array<SkyPart, 4> kParts { {
            { &SkyModels::dome, "SkySphere", "SkyColour" },
            { &SkyModels::stars, "StarSphere", "Stars" },
            { &SkyModels::milky_way, "MilkyWay", "Milkyway" },
            { &SkyModels::clouds, "Clouds", "Clouds" },
        } };

        /// Prefix of the sky's texture keys, so they never meet other pictures' names.
        constexpr std::string_view kKeyPrefix = "sky:";

        SkyShape shape_of(const ovl::StaticShape& shape, std::string key)
        {
            SkyShape out;
            out.texture = std::move(key);
            for (const ovl::ShapeMesh& mesh : shape.meshes)
            {
                const auto base = static_cast<std::uint32_t>(out.vertices.size());
                for (std::size_t i = 0; i < mesh.vertex_count; ++i)
                {
                    const auto v = mesh.vertex(i);
                    out.vertices.push_back({ { v.position[0], v.position[1], v.position[2] }, { v.normal[0], v.normal[1], v.normal[2] }, { v.texcoord[0], v.texcoord[1] } });
                }
                for (std::size_t i = 0; i + 2 < mesh.index_count; i += 3)
                {
                    const std::array<std::uint32_t, 3> t { mesh.index(i), mesh.index(i + 1), mesh.index(i + 2) };
                    if (t[0] >= mesh.vertex_count || t[1] >= mesh.vertex_count || t[2] >= mesh.vertex_count)
                        continue;
                    for (const std::uint32_t k : t)
                        out.indices.push_back(base + k);
                }
            }
            return out;
        }

    } // namespace

    std::expected<LoadedSky, std::string> load_sky(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);

        LoadedSky sky;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::StaticShapeItem& item : ovl::read_static_shapes(*overlay, *part))
            {
                for (const SkyPart& p : kParts)
                {
                    if (item.shape && item.name == p.shape)
                        sky.models.*p.slot = shape_of(*item.shape, std::string(kKeyPrefix) + std::string(p.picture));
                }
            }
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                for (const SkyPart& p : kParts)
                {
                    if (!item.texture || item.name != p.picture)
                        continue;
                    if (auto image = pixels.first_frame(*item.texture))
                        sky.images.insert_or_assign(std::string(kKeyPrefix) + std::string(p.picture), std::move(*image));
                }
            }
        }
        return sky;
    }

    std::optional<glm::vec4> horizon_colour(const LoadedSky& sky, float day)
    {
        const SkyShape& dome = sky.models.dome;
        const auto image = sky.images.find(dome.texture);
        if (dome.vertices.empty() || image == sky.images.end() || image->second.width == 0 || image->second.height == 0)
            return std::nullopt;
        const ovl::RgbaImage& picture = image->second;
        const auto lowest = std::ranges::min_element(dome.vertices, { }, [](const ModelVertex& v) { return v.position.y; });
        const auto texel = [](float t, std::uint32_t size) {
            const float wrapped = t - std::floor(t);
            return std::min(static_cast<std::uint32_t>(wrapped * static_cast<float>(size)), size - 1);
        };
        const std::size_t at = (static_cast<std::size_t>(texel(lowest->uv.y, picture.height)) * picture.width + texel(day, picture.width)) * 4;
        if (at + 3 >= picture.pixels.size())
            return std::nullopt;
        return glm::vec4(picture.pixels[at], picture.pixels[at + 1], picture.pixels[at + 2], 255) / 255.0f;
    }

} // namespace rct3::park
