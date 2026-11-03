// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_flow.hpp"

#include "ovl/overlay.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
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

    std::optional<ModelPart> flow_strip(const ovl::Spline& left, const ovl::Spline& right, const ovl::Spline& track, std::string texture)
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
        // Files list the water of some pieces from the track's far end (all right-hand curves, among others).
        const bool backwards = !track.nodes.empty() && glm::distance(lefts.back(), vec(track.nodes.front().position)) < glm::distance(lefts.front(), vec(track.nodes.front().position));
        ModelPart part;
        part.texture = std::move(texture);
        part.blend = PartBlend::Blended;
        part.chain = true;
        for (std::size_t i = 0; i < lefts.size(); ++i)
        {
            const std::size_t next = std::min(i + 1, lefts.size() - 1);
            const std::size_t back = next == i ? i - 1 : i;
            glm::vec3 normal = glm::cross(lefts[next] + rights[next] - lefts[back] - rights[back], rights[i] - lefts[i]);
            if (glm::length(normal) > 0)
                normal = glm::normalize(normal);
            if (normal.y < 0)
                normal = -normal;
            const float v = (backwards ? along.back() - along[i] : along[i]) / along.back() * repeats;
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

    void flowing_sheet(ModelPart& sheet, const ovl::Spline& track, std::string texture)
    {
        sheet.texture = std::move(texture);
        sheet.blend = PartBlend::Blended;
        sheet.chain = true;
        if (sheet.vertices.empty() || track.nodes.empty())
            return;
        const auto [low, high] = std::ranges::minmax_element(sheet.vertices, { }, [](const ModelVertex& v) { return v.uv.y; });
        const glm::vec3 start = vec(track.nodes.front().position);
        if (glm::distance(high->position, start) >= glm::distance(low->position, start))
            return;
        const float ends = low->uv.y + high->uv.y;
        for (ModelVertex& v : sheet.vertices)
            v.uv.y = ends - v.uv.y;
    }

    float flow_scroll(float seconds, std::int32_t run)
    {
        // The picture's v goes down by the distance flowed, so it is read further back and moves on along v.
        const float moved = seconds * kFlowSpeed / kFlowRepeat * (run < 0 ? 1.0f : -1.0f);
        return moved - std::floor(moved);
    }

    std::expected<ovl::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file, std::string_view name)
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
                if (item.texture && strings::equals_ignoring_case(item.name, name))
                {
                    if (auto image = pixels.first_frame(*item.texture))
                        return std::move(*image);
                }
            }
        }
        return std::unexpected(std::format("{} is not in {}", name, file.string()));
    }

} // namespace rct3::park
