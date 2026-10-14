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
            bool glows = false; ///< Its picture is light on black, with no see-through parts of its own
        };

        constexpr std::array<SkyPart, 4> kParts { {
            { &SkyModels::dome, "SkySphere", "SkyColour" },
            { &SkyModels::stars, "StarSphere", "Stars", true },
            { &SkyModels::milky_way, "MilkyWay", "Milkyway" },
            { &SkyModels::clouds, "Clouds", "Clouds" },
        } };

        /// Prefix of the sky's texture keys, so they never meet other pictures' names.
        constexpr std::string_view kKeyPrefix = "sky:";
        constexpr std::string_view kMountainShape = "Mountains";

        std::string key(std::string_view picture)
        {
            return std::string(kKeyPrefix) + std::string(picture);
        }

        /// Adds `mesh`'s vertices and whole triangles to `out`.
        void add_mesh(SkyShape& out, const ovl::ShapeMesh& mesh)
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

        SkyShape mesh_of(const ovl::ShapeMesh& mesh, std::string key)
        {
            SkyShape out;
            out.texture = std::move(key);
            add_mesh(out, mesh);
            return out;
        }

        SkyShape shape_of(const ovl::StaticShape& shape, std::string key)
        {
            SkyShape out;
            out.texture = std::move(key);
            for (const ovl::ShapeMesh& mesh : shape.meshes)
                add_mesh(out, mesh);
            return out;
        }

    } // namespace

    void glowing(ovl::RgbaImage& image)
    {
        for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4)
        {
            std::uint8_t* p = &image.pixels[i];
            const std::uint8_t brightest = std::max({ p[0], p[1], p[2] });
            if (brightest == 0)
            {
                p[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>(p[c] * 255 / brightest);
            p[3] = static_cast<std::uint8_t>(p[3] * brightest / 255);
        }
    }

    std::expected<LoadedSky, std::string> load_sky(const std::filesystem::path& path, std::string_view theme)
    {
        const std::array<std::string, 2> panels = mountain_pictures(theme);
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
                        sky.models.*p.slot = shape_of(*item.shape, key(p.picture));
                }
                if (item.shape && item.name == kMountainShape)
                {
                    for (std::size_t m = 0; m < sky.mountains.size() && m < item.shape->meshes.size(); ++m)
                        sky.mountains[m] = mesh_of(item.shape->meshes[m], key(panels[m]));
                }
            }
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (!item.texture)
                    continue;
                const auto part_of = std::ranges::find(kParts, item.name, &SkyPart::picture);
                const bool wanted = part_of != kParts.end() || std::ranges::find(panels, item.name) != panels.end();
                if (!wanted)
                    continue;
                if (auto image = pixels.first_frame(*item.texture))
                {
                    if (part_of != kParts.end() && part_of->glows)
                        glowing(*image);
                    sky.images.insert_or_assign(key(item.name), std::move(*image));
                }
            }
        }
        return sky;
    }

    std::vector<glm::vec4> horizon_band(const LoadedSky& sky)
    {
        const SkyShape& dome = sky.models.dome;
        const auto image = sky.images.find(dome.texture);
        if (dome.vertices.empty() || image == sky.images.end() || image->second.width == 0 || image->second.height == 0)
            return { };
        const ovl::RgbaImage& picture = image->second;
        const auto lowest = std::ranges::min_element(dome.vertices, { }, [](const ModelVertex& v) { return v.position.y; });
        const float down = lowest->uv.y - std::floor(lowest->uv.y);
        const auto row = std::min(static_cast<std::size_t>(down * static_cast<float>(picture.height)), static_cast<std::size_t>(picture.height) - 1);
        const std::size_t start = row * picture.width * 4;
        if (start + static_cast<std::size_t>(picture.width) * 4 > picture.pixels.size())
            return { };
        std::vector<glm::vec4> band;
        band.reserve(picture.width);
        for (std::size_t at = start; at < start + static_cast<std::size_t>(picture.width) * 4; at += 4)
            band.push_back(glm::vec4(picture.pixels[at], picture.pixels[at + 1], picture.pixels[at + 2], 255) / 255.0f);
        return band;
    }

} // namespace rct3::park
