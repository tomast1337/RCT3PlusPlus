// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water/flow.hpp"

#include "ovl/overlay.hpp"
#include "ovl/texture/pixels.hpp"
#include "ovl/texture/texture.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::park::water
{

    namespace
    {

        constexpr int kSteps = 4; ///< Samples to each part of the splines

        glm::vec3 vec(const std::array<float, 3>& p)
        {
            return { p[0], p[1], p[2] };
        }

    } // namespace

    std::optional<object::ModelPart> flow_strip(const ovl::Spline& left, const ovl::Spline& right, const ovl::Spline& track, std::string texture)
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
        object::ModelPart part;
        part.texture = std::move(texture);
        part.blend = object::PartBlend::Blended;
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
            // Samples are spaced evenly between nodes, so a sample's place counts nodes.
            const float node = static_cast<float>(i) / static_cast<float>(lefts.size() - 1);
            part.flow.insert(part.flow.end(), 2, backwards ? 1 - node : node);
        }
        part.flow_last = 1.0f / static_cast<float>(left.nodes.size() - 1);
        for (std::uint32_t i = 0; i + 1 < lefts.size(); ++i)
        {
            const std::uint32_t a = i * 2;
            part.indices.insert(part.indices.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }
        return part;
    }

    void flowing_sheet(object::ModelPart& sheet, const ovl::Spline& track, std::string texture)
    {
        sheet.texture = std::move(texture);
        sheet.blend = object::PartBlend::Blended;
        sheet.chain = true;
        if (sheet.vertices.empty() || track.nodes.empty())
            return;
        const auto [low, high] = std::ranges::minmax_element(sheet.vertices, { }, [](const object::ModelVertex& v) { return v.uv.y; });
        const glm::vec3 start = vec(track.nodes.front().position);
        if (glm::distance(high->position, start) >= glm::distance(low->position, start))
            return;
        const float ends = low->uv.y + high->uv.y;
        for (object::ModelVertex& v : sheet.vertices)
            v.uv.y = ends - v.uv.y;
    }

    object::FlowFades flow_fades(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const std::function<bool(std::size_t)>& pool_slides)
    {
        std::map<std::uint64_t, bool> wet;
        for (const save::model::Ride& ride : rides.rides)
            if (ride.track)
                for (const save::model::TrackPiece& piece : ride.track->pieces)
                    wet.emplace(piece.scenery, false);
        for (const save::model::PlacedObject& o : objects)
            if (const auto at = wet.find(o.id); at != wet.end())
                if (const auto model = models.find(object::model_ref(o)); model != models.end())
                    at->second = model->second.flows;
        object::FlowFades fades;
        for (std::size_t r = 0; r < rides.rides.size(); ++r)
        {
            if (!rides.rides[r].track)
                continue;
            const save::model::Track& track = *rides.rides[r].track;
            const std::size_t count = track.pieces.size();
            const auto wet_at = [&](std::size_t i) { return track.pieces[i].scenery != 0 && wet[track.pieces[i].scenery]; };
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!wet_at(i))
                    continue;
                const bool before = i > 0 ? wet_at(i - 1) : track.circuit && wet_at(count - 1);
                const bool after = i + 1 < count ? wet_at(i + 1) : track.circuit && wet_at(0);
                object::FlowFade fade { .in = !before, .out = !after };
                // A pool slide's open end runs full to its last stretch and stops there.
                if (i + 1 == count && !track.circuit && pool_slides(r))
                    fade = { .in = fade.in, .out = false, .end = true };
                if (track.pieces[i].reversed)
                    std::swap(fade.in, fade.out);
                if (fade.in || fade.out || fade.end)
                    fades[track.pieces[i].scenery] = fade;
            }
        }
        return fades;
    }

    float flow_alpha(const object::FlowFade& fade, float along, float last)
    {
        float alpha = 1;
        if (fade.in && fade.out)
            alpha = 1 - std::abs(2 * along - 1);
        else if (fade.in)
            alpha = along;
        else if (fade.out)
            alpha = 1 - along;
        if (fade.end && last > 0)
            alpha *= std::clamp((1 - along) / last, 0.0f, 1.0f);
        return alpha;
    }

    TextureShift flow_scroll(float seconds, std::int32_t run)
    {
        // The picture's v goes down by the distance flowed, so it is read further back and moves on along v.
        return TextureShift::by(seconds * kFlowSpeed / kFlowRepeat * (run < 0 ? 1.0f : -1.0f));
    }

    std::expected<ovl::texture::RgbaImage, std::string> load_flow_picture(const std::filesystem::path& file, std::string_view name)
    {
        auto overlay = ovl::Overlay::load(file);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        const ovl::texture::TexturePixels pixels = ovl::texture::TexturePixels::index(*overlay);
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::texture::TextureItem& item : ovl::texture::read_textures(*overlay, *part))
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

} // namespace rct3::park::water
