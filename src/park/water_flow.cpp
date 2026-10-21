// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_flow.hpp"

#include "ovl/overlay.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace rct3::park
{

    namespace
    {

        constexpr int kSteps = 4; ///< Samples to each part of the splines

        glm::vec3 vec(const std::array<float, 3>& p)
        {
            return { p[0], p[1], p[2] };
        }

    } // namespace

    std::optional<ModelPart> flow_strip(const ovl::Spline& left, const ovl::Spline& right, std::string texture)
    {
        if (left.nodes.size() < 2 || left.nodes.size() != right.nodes.size())
            return std::nullopt;
        std::vector<glm::vec3> lefts;
        std::vector<glm::vec3> rights;
        for (std::size_t s = 0; s + 1 < left.nodes.size(); ++s)
        {
            for (int k = s == 0 ? 0 : 1; k <= kSteps; ++k)
            {
                const float t = static_cast<float>(k) / kSteps;
                lefts.push_back(vec(ovl::spline_point(left, s, t)));
                rights.push_back(vec(ovl::spline_point(right, s, t)));
            }
        }
        std::vector<float> along(lefts.size(), 0.0f);
        for (std::size_t i = 1; i < lefts.size(); ++i)
            along[i] = along[i - 1] + glm::distance((lefts[i] + rights[i]) / 2.0f, (lefts[i - 1] + rights[i - 1]) / 2.0f);
        if (along.back() <= 0)
            return std::nullopt;
        // A whole number of repeats, so the picture meets the next piece's.
        const float repeats = std::max(1.0f, std::round(along.back() / kFlowRepeat));
        ModelPart part;
        part.texture = std::move(texture);
        part.blend = PartBlend::Blended;
        for (std::size_t i = 0; i < lefts.size(); ++i)
        {
            const std::size_t next = std::min(i + 1, lefts.size() - 1);
            const std::size_t back = next == i ? i - 1 : i;
            glm::vec3 normal = glm::cross(lefts[next] + rights[next] - lefts[back] - rights[back], rights[i] - lefts[i]);
            if (glm::length(normal) > 0)
                normal = glm::normalize(normal);
            if (normal.y < 0)
                normal = -normal;
            const float v = along[i] / along.back() * repeats;
            part.vertices.push_back({ lefts[i], normal, { 0.0f, v } });
            part.vertices.push_back({ rights[i], normal, { 1.0f, v } });
        }
        for (std::uint32_t i = 0; i + 1 < lefts.size(); ++i)
        {
            const std::uint32_t a = i * 2;
            part.indices.insert(part.indices.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }
        return part;
    }

    std::uint32_t flow_shift(std::uint32_t rows, float seconds)
    {
        if (rows == 0)
            return 0;
        const float moved = std::fmod(seconds * kFlowSpeed / kFlowRepeat, 1.0f);
        return static_cast<std::uint32_t>(moved * static_cast<float>(rows)) % rows;
    }

    ovl::RgbaImage flow_rows(const ovl::RgbaImage& image, std::uint32_t shift)
    {
        ovl::RgbaImage out { image.width, image.height, std::vector<std::uint8_t>(image.pixels.size()) };
        const std::size_t row = std::size_t { image.width } * 4;
        if (image.height == 0 || image.pixels.size() != row * image.height)
            return image;
        for (std::uint32_t y = 0; y < image.height; ++y)
            std::memcpy(out.pixels.data() + ((y + shift) % image.height) * row, image.pixels.data() + y * row, row);
        return out;
    }

    std::expected<ovl::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file)
    {
        auto overlay = ovl::Overlay::load(file);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
            {
                if (item.texture && strings::equals_ignoring_case(item.name, kFlowTexture))
                {
                    if (auto image = pixels.first_frame(*item.texture))
                        return std::move(*image);
                }
            }
        }
        return std::unexpected(std::format("{} is not in {}", kFlowTexture, file.string()));
    }

} // namespace rct3::park
