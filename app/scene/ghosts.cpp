// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ghosts.hpp"

#include "park/object/models.hpp"
#include "park/relief_maps.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        /// The texture a ghost draws `batch` with. Ghosts are drawn see-through, which takes a picture's alpha
        /// as coverage; opaque parts may keep other things in their alpha (a path's surface does), so they get
        /// a copy of their own with it filled.
        std::string ghost_texture(const rct3::park::object::ObjectBatch& batch)
        {
            return batch.blend == rct3::park::object::PartBlend::Opaque ? std::format("ghost:{}", batch.texture) : batch.texture;
        }

    } // namespace

    void ghost_textures(render::Device& device, park::ParkScene& scene, const rct3::park::object::ObjectMesh& ghost)
    {
        std::vector<rct3::park::object::ObjectBatch> missing;
        for (const rct3::park::object::ObjectBatch& batch : ghost.batches)
        {
            if (!scene.object_textures.contains(ghost_texture(batch)))
                missing.push_back(batch);
        }
        if (missing.empty())
            return;
        auto pictures = rct3::park::object::paint_batches(missing, scene.pictures);
        for (const rct3::park::object::ObjectBatch& batch : missing)
        {
            const auto painted = pictures.find(batch.texture);
            if (batch.blend != rct3::park::object::PartBlend::Opaque || painted == pictures.end())
                continue;
            ovl::texture::RgbaImage filled = painted->second;
            for (std::size_t i = 3; i < filled.pixels.size(); i += 4)
                filled.pixels[i] = 255;
            pictures.insert_or_assign(ghost_texture(batch), std::move(filled));
        }
        park::fit_texture_detail(scene, pictures);
        if (scene.relief_maps)
            rct3::park::add_relief_maps(pictures); // The pictures stay for the object once it is built
        std::erase_if(pictures, [&](const auto& entry) { return scene.object_textures.contains(entry.first); });
        auto uploaded = ui::upload_textures(device, pictures);
        if (uploaded)
            scene.object_textures.merge(*uploaded);
    }

    render::SceneLight ghost_light(render::SceneLight light, float clock)
    {
        // The strength swings between these, rising and falling evenly, this many times a second.
        constexpr float kDimmest = 0.7f;
        constexpr float kBrightest = 1.1f;
        constexpr float kPulses = 0.6f;
        const float wave = 2 * std::abs(clock * kPulses - std::floor(clock * kPulses) - 0.5f); // 1 to 0 and back
        light.sky = glm::vec3(std::lerp(kDimmest, kBrightest, wave));
        light.ghost = true;
        light.haze_rate = 0;
        light.shadow_map = { };
        light.shadow_view = { };
        light.shadow_map_before = { };
        light.lamps = { };
        return light;
    }

    void draw_ghost_mesh(render::Device& device, const park::ParkScene& scene, const rct3::park::object::ObjectMesh& mesh, const glm::mat4& transform, const render::SceneLight& light, bool solid)
    {
        if (mesh.indices.empty() || !scene.see_through_pipeline)
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
        const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
        if (!vertices || !indices)
            return;
        for (const rct3::park::object::ObjectBatch& batch : mesh.batches)
        {
            if (batch.blend == rct3::park::object::PartBlend::DepthOnly) // It would hide the ghost's own insides
                continue;
            const auto texture = scene.object_textures.find(ghost_texture(batch));
            render::DrawCommand draw;
            draw.pipeline = !solid || batch.blend == rct3::park::object::PartBlend::Blended ? scene.see_through_pipeline : (batch.blend == rct3::park::object::PartBlend::Cutout ? scene.cutout_pipeline : scene.object_pipeline);
            draw.vertices = *vertices;
            draw.indices = *indices;
            draw.index_type = render::IndexType::U32;
            draw.first = batch.first;
            draw.count = batch.count;
            draw.transform = transform;
            draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            draw.light = solid ? light : ghost_light(light, scene.clock);
            device.draw(draw);
        }
    }

} // namespace rct3::scene
