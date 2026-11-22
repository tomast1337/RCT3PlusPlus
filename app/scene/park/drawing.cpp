// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "ovl/texture/image.hpp"
#include "park/chain_lifts.hpp"
#include "park/daylight.hpp"
#include "park/flexi_colours.hpp"
#include "park/ground/textures.hpp"
#include "park/lamp_grid.hpp"
#include "park/object/models.hpp"
#include "park/person/mesh.hpp"
#include "park/person/motions.hpp"
#include "park/sun_view.hpp"
#include "park/water/flow.hpp"
#include "park/water/jet.hpp"
#include "profiling/profiler.hpp"
#include "scene/ghosts.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/glow.hpp"
#include "scene/park/occlusion.hpp"
#include "scene/park/particles.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/poses.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "scene/park/water.hpp"
#include "scene/pool_tools.hpp"
#include "ui/screens/build/pools.hpp"
#include "units/units.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        // Size of photos at photo spots.
        constexpr u32 kPhotoWidth = 640;
        constexpr u32 kPhotoHeight = 480;

        /// `mesh` in the frame's memory, with its vertices as `vertex_bytes` when given, for pipelines of another
        /// layout. Copied there the first time the frame asks and kept in `uploads` for the passes after; copied each
        /// time without them.
        std::optional<FrameUpload> upload_once(render::Device& device, std::optional<std::vector<FrameUpload>&> uploads, const rct3::park::object::ObjectMesh& mesh, std::span<const std::byte> vertex_bytes = { })
        {
            if (uploads)
                if (const auto kept = std::ranges::find(*uploads, &mesh, &FrameUpload::mesh); kept != uploads->end())
                    return *kept;
            const auto vertices = device.allocate_transient(vertex_bytes.empty() ? std::as_bytes(std::span(mesh.vertices)) : vertex_bytes);
            const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
            if (!vertices || !indices)
                return std::nullopt;
            const FrameUpload made { .mesh = &mesh, .vertices = *vertices, .indices = *indices };
            if (uploads)
                uploads->push_back(made);
            return made;
        }

        /// Draws `mesh` from the frame's memory (upload_once), each batch with `pipeline_of(batch)`, none leaving it out.
        void draw_transient(render::Device& device, const ParkScene& scene, std::optional<std::vector<FrameUpload>&> uploads, const rct3::park::object::ObjectMesh& mesh, const ui::UiTextures& textures, const glm::mat4& transform, const render::SceneLight& light, auto&& pipeline_of, auto&& address_of)
        {
            if (mesh.indices.empty())
                return;
            const std::optional<FrameUpload> uploaded = upload_once(device, uploads, mesh);
            if (!uploaded)
                return;
            for (const rct3::park::object::ObjectBatch& batch : mesh.batches)
            {
                const auto texture = textures.find(batch.texture);
                render::DrawCommand draw;
                draw.pipeline = pipeline_of(batch);
                if (!draw.pipeline)
                    continue;
                draw.vertices = uploaded->vertices;
                draw.indices = uploaded->indices;
                draw.index_type = render::IndexType::U32;
                draw.first = batch.first;
                draw.count = batch.count;
                draw.transform = transform;
                draw.texture = texture != textures.end() ? texture->second.handle : scene.blank;
                draw.filter = render::Filter::Linear;
                draw.address = address_of(batch);
                draw.light = light;
                device.draw(draw);
            }
        }

        /// Which of the things casting shadows a draw takes: the shadow keeps those standing still in a drawing of
        /// their own, and draws only the others each frame.
        enum class Casters : u8
        {
            All,
            Still,  ///< The ground, objects, billboards, signs and crates
            Moving, ///< People, animals, cars and rides
        };

        /// Which parts of the world a draw shows: a reflection leaves some out, as the options ask.
        struct WorldParts
        {
            bool background = true; ///< Sky and mountains
            bool terrain = true;
            bool scenery = true;
            bool people = true;
            bool guests = true;  ///< Of the people, the park's guests and staff; a photo draws its own
            bool animals = true; ///< The zoo's
            bool water = true;
            bool see_through = true;   ///< Blended parts, such as glass
            bool shaded_water = false; ///< With the water shader, seen from `eye`
            bool ghosts = false;       ///< What the build tools would place: never in the shadows, which would show it laid
            bool faded_ground = false; ///< The ground's land, cliffs and tunnels drawn last, see-through
            bool mirrored = false;     ///< In a lake's reflection, where the waterfalls show without their water
            Casters casters = Casters::All;
            glm::vec3 eye { 0.0f };
        };

        /// The park's ground, objects, water, sky and mountains as `transform` sees them, lit by `light`.
        void draw_world(render::Device& device, ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light, const WorldParts& parts = { })
        {
            if (scene.sky_pipeline && parts.background)
            {
                const profiling::Scope zone("Sky");
                // The dome reads one column of its picture, so it must not wrap into the next.
                draw_transient(device, scene, scene.frame_uploads, scene.sky_mesh, scene.sky_textures, transform, light, [&](const rct3::park::object::ObjectBatch&) { return scene.sky_pipeline; }, [&](const rct3::park::object::ObjectBatch& b) { return b.texture == scene.sky.dome.texture ? render::AddressMode::Clamp : render::AddressMode::Repeat; });
            }
            // Before the ground, writing depth, so land reaching beyond the ring stays hidden behind it.
            if (parts.background)
                profiling::section("Mountains", [&] { draw_transient(device, scene, scene.frame_uploads, scene.mountains, scene.sky_textures, transform, light, [&](const rct3::park::object::ObjectBatch&) { return scene.cutout_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; }); });
            const auto pipeline_for = [&](bool crossing, rct3::park::object::PartBlend blend, std::string_view texture = { }) {
                if (texture == rct3::park::object::kChromePicture && blend != rct3::park::object::PartBlend::DepthOnly)
                    return crossing ? scene.crossing_chrome_pipeline : scene.chrome_pipeline;
                if (blend == rct3::park::object::PartBlend::Opaque)
                    return crossing ? scene.crossing_pipeline : scene.object_pipeline;
                if (blend == rct3::park::object::PartBlend::Cutout)
                    return crossing ? scene.crossing_cutout_pipeline : scene.cutout_pipeline;
                if (blend == rct3::park::object::PartBlend::DepthOnly)
                    return crossing ? scene.crossing_depth_only_pipeline : scene.depth_only_pipeline;
                return crossing ? scene.crossing_see_through_pipeline : scene.see_through_pipeline;
            };
            const auto sided = [&](render::PipelineHandle pipeline, const rct3::park::object::ObjectBatch& batch) { return batch.one_sided ? scene.front_only.at(pipeline.id) : pipeline; };
            // Depth-only parts are left to their own pass, after the solid ones.
            const auto object_pipeline = [&](const rct3::park::object::ObjectBatch& b) { return b.blend == rct3::park::object::PartBlend::DepthOnly ? render::PipelineHandle { } : sided(pipeline_for(false, b.blend, b.texture), b); };
            const auto depth_pipeline = [&](const rct3::park::object::ObjectBatch& b) { return b.blend == rct3::park::object::PartBlend::DepthOnly ? sided(scene.depth_only_pipeline, b) : render::PipelineHandle { }; };
            // Slides' flowing water takes the water shader with the water, unless crossing a band of detail: those
            // objects' vertices carry the band too, which the water shader does not read.
            const bool shaded_flow = parts.shaded_water && scene.water.shaded && parts.water && scene.show_scenery && parts.scenery && parts.see_through;
            // Opaque and cut-out parts in the solid pass, the others in their own.
            const bool still = parts.casters != Casters::Moving;
            const bool moving = parts.casters != Casters::Still;
            const auto draw_shown = [&](usize set, std::optional<rct3::park::object::PartBlend> pass) {
                const bool crossing = set != 0;
                for (const ChunkBatch& at : scene.object_order[set])
                {
                    const ShownObjects& shown = scene.object_chunks[at.chunk].*kChunkSets[set];
                    const rct3::park::object::ObjectBatch& batch = shown.batches[at.batch];
                    if (scene.chunks_seen[at.chunk] == 0 || !shown.indices)
                        continue;
                    if (pass ? batch.blend != *pass : batch.blend == rct3::park::object::PartBlend::Blended || batch.blend == rct3::park::object::PartBlend::DepthOnly)
                        continue;
                    if (shaded_flow && !crossing && batch.texture == rct3::park::water::kFlowPicture)
                        continue;
                    // Parts painted like the ground take the ground's pictures (see object/mesh.hpp).
                    const ui::UiTextures& textures = scene.object_textures.contains(batch.texture) ? scene.object_textures : scene.ground_textures;
                    const auto texture = textures.find(batch.texture);
                    render::DrawCommand draw;
                    draw.pipeline = sided(pipeline_for(crossing, batch.blend, batch.texture), batch);
                    draw.vertices = { shown.vertices };
                    draw.indices = { shown.indices };
                    draw.index_type = render::IndexType::U32;
                    draw.first = batch.first;
                    draw.count = batch.count;
                    draw.transform = transform;
                    draw.texture = texture != textures.end() ? texture->second.handle : scene.blank;
                    draw.filter = render::Filter::Linear;
                    draw.address = render::AddressMode::Repeat;
                    draw.light = light;
                    draw.extra_textures[1] = relief_of(textures, batch.texture);
                    draw.v_shift = batch.texture == rct3::park::water::kFlowPicture ? rct3::park::water::flow_scroll(scene.park_clock, batch.chain) : rct3::park::chain_scroll(scene.chain_shifts, batch.chain);
                    device.draw(draw);
                }
            };
            // Land, cliffs and tunnels, as against the paths and queues drawn on the ground.
            const auto earth = [](const rct3::park::GroundBatch& batch) { return batch.paint == rct3::park::GroundPaint::Surface || batch.paint == rct3::park::GroundPaint::Cliff || batch.paint == rct3::park::GroundPaint::TunnelWall || batch.paint == rct3::park::GroundPaint::TunnelCeiling; };
            const auto draw_ground = [&](render::PipelineHandle pipeline, auto&& wanted) {
                for (const GroundChunk& chunk : scene.ground_chunks)
                {
                    if (!chunk.vertices || !rct3::park::box_in_view(transform, chunk.low, chunk.high))
                        continue;
                    for (const rct3::park::GroundBatch& batch : chunk.batches)
                    {
                        if (!wanted(batch))
                            continue;
                        const auto texture = scene.ground_textures.find(rct3::park::ground::ground_key(batch.paint, batch.index));
                        render::DrawCommand draw;
                        draw.pipeline = pipeline;
                        draw.vertices = { chunk.vertices };
                        draw.indices = { chunk.indices };
                        draw.index_type = render::IndexType::U32;
                        draw.first = batch.first;
                        draw.count = batch.count;
                        draw.transform = transform;
                        draw.texture = texture != scene.ground_textures.end() ? texture->second.handle : scene.blank;
                        draw.filter = render::Filter::Linear;
                        draw.address = render::AddressMode::Repeat;
                        draw.light = light;
                        draw.light.cut_clear = !earth(batch); // The grass round a path's shape stays clear of its depth
                        draw.extra_textures[0] = scene.variation;
                        draw.extra_textures[1] = relief_of(scene.ground_textures, rct3::park::ground::ground_key(batch.paint, batch.index));
                        device.draw(draw);
                    }
                }
            };
            if (parts.terrain && scene.show_ground && still)
                profiling::section("Ground", [&] {
                    draw_ground(scene.ground_pipeline, [&](const rct3::park::GroundBatch& batch) { return !parts.faded_ground || !earth(batch); });
                    draw_view_lines(device, scene, transform, light);
                    if (parts.ghosts)
                        draw_footpath_ghost(device, scene, transform, light);
                    draw_patrol(device, scene, transform, light);
                });
            // Only the chunks the view reaches: for a shadow map, those in the sun's box round the camera, so ones
            // out of the camera's view still cast into it.
            scene.chunks_seen.resize(scene.object_chunks.size());
            for (usize c = 0; c < scene.object_chunks.size(); ++c)
            {
                const rct3::park::object::ShapeBox& box = scene.object_chunks[c].box;
                scene.chunks_seen[c] = !box.empty && rct3::park::box_in_view(transform, box.low, box.high) ? 1 : 0;
            }
            const auto draw_objects = [&](std::optional<rct3::park::object::PartBlend> pass) {
                for (usize set = 0; set < kChunkSets.size(); ++set)
                    draw_shown(set, pass);
            };
            if (scene.show_scenery && parts.scenery && still)
                profiling::section("Objects", [&] {
                    draw_objects(std::nullopt);
                    draw_transient(device, scene, scene.frame_uploads, scene.fresh_objects, scene.object_textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                });
            if (scene.show_scenery && parts.scenery && still)
                profiling::section("Billboards", [&] {
                    for (usize c = 0; c < scene.object_chunks.size(); ++c)
                    {
                        const ObjectChunk& chunk = scene.object_chunks[c];
                        if (scene.chunks_seen[c] == 0)
                            continue;
                        for (const ShownObjects* shown : { &chunk.steady, &chunk.finer, &chunk.coarser })
                        {
                            const bool crossing = shown != &chunk.steady;
                            // shortcut: crossing billboards are built with their bands each frame; fine for the
                            // few in bands, upgrade to a second vertex stream if many trees cross at once.
                            if (!shown->billboards.mesh.indices.empty() && !std::ranges::contains(scene.frame_uploads, &shown->billboards.mesh, &FrameUpload::mesh))
                            {
                                crossing_vertices(scene.crossing_scratch, shown->billboards.mesh.vertices, shown->bands);
                                upload_once(device, scene.frame_uploads, shown->billboards.mesh, std::as_bytes(std::span(scene.crossing_scratch)));
                            }
                            draw_transient(device, scene, scene.frame_uploads, shown->billboards.mesh, scene.object_textures, transform, light, [&](const rct3::park::object::ObjectBatch& b) { return sided(pipeline_for(crossing, b.blend), b); }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
                        }
                    }
                });
            if (scene.show_people && parts.people && parts.guests && moving)
                profiling::section("People", [&] {
                    draw_transient(device, scene, scene.frame_uploads, scene.still_people, scene.people_textures, transform, light, [&](const rct3::park::object::ObjectBatch&) { return scene.object_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
                    draw_transient(device, scene, scene.frame_uploads, scene.moving_people, scene.people_textures, transform, light, [&](const rct3::park::object::ObjectBatch&) { return scene.object_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
                    if (scene.simulate)
                        draw_transient(device, scene, scene.frame_uploads, scene.riders.mesh, scene.people_textures, transform, light, [&](const rct3::park::object::ObjectBatch&) { return scene.object_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
                });
            if (parts.animals && moving)
                profiling::section("ZooAnimals", [&] { draw_transient(device, scene, scene.frame_uploads, scene.drawn_animals.mesh, scene.drawn_animals.textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; }); });
            if (parts.people && moving)
                profiling::section("WildBirds", [&] { draw_transient(device, scene, scene.frame_uploads, scene.drawn_birds.mesh, scene.drawn_birds.textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; }); });
            if (scene.show_scenery && parts.scenery && still)
                profiling::section("LedSigns", [&] { draw_led_signs(device, scene, transform, light); });
            if (scene.show_scenery && parts.scenery && moving)
                profiling::section("Cars", [&] {
                    draw_transient(device, scene, scene.frame_uploads, scene.car_meshes.mesh, scene.object_textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                    for (const ParkScene::BonedCar& boned : scene.boned_cars)
                        draw_transient(device, scene, scene.frame_uploads, boned.mesh.mesh, scene.object_textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                });
            if (scene.show_scenery && parts.scenery && still && !scene.drawn_animals.crated.empty())
                profiling::section("Crates", [&] { draw_transient(device, scene, scene.frame_uploads, scene.drawn_animals.crates.mesh, scene.object_textures, transform, light, object_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; }); });
            const auto draw_rides = [&](auto&& pipeline_of) {
                for (const ParkScene::BonedRide& boned : scene.boned_rides)
                    if ((boned.ride != entt::null && !boned.gate) || maybe_in_view(transform, glm::vec3(boned.placed[3]), boned.reach))
                        draw_transient(device, scene, scene.frame_uploads, boned.mesh.mesh, scene.object_textures, transform, light, pipeline_of, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
            };
            if (moving)
                profiling::section("Rides", [&] { draw_rides(object_pipeline); });
            // After everything solid, before the water and the see-through parts it is there to hide.
            if (scene.show_scenery && parts.scenery && parts.water)
                profiling::section("DepthOnly", [&] {
                    draw_objects(rct3::park::object::PartBlend::DepthOnly);
                    draw_transient(device, scene, scene.frame_uploads, scene.fresh_objects, scene.object_textures, transform, light, depth_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                    draw_transient(device, scene, scene.frame_uploads, scene.car_meshes.mesh, scene.object_textures, transform, light, depth_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                    for (const ParkScene::BonedCar& boned : scene.boned_cars)
                        draw_transient(device, scene, scene.frame_uploads, boned.mesh.mesh, scene.object_textures, transform, light, depth_pipeline, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Repeat; });
                    draw_rides(depth_pipeline);
                });
            if (parts.water)
                profiling::section("Water", [&] {
                    draw_park_water(device, scene.water, transform, light, parts.eye, scene.park_clock, parts.shaded_water);
                    const auto flow = scene.object_textures.find(rct3::park::water::kFlowPicture);
                    if (!shaded_flow || flow == scene.object_textures.end())
                        return;
                    for (const ChunkBatch& at : scene.object_order[0])
                    {
                        const ObjectChunk& chunk = scene.object_chunks[at.chunk];
                        const rct3::park::object::ObjectBatch& batch = chunk.steady.batches[at.batch];
                        if (batch.texture != rct3::park::water::kFlowPicture || !chunk.steady.indices || scene.chunks_seen[at.chunk] == 0)
                            continue;
                        render::DrawCommand draw { .pipeline = scene.water.shaded, .vertices = { chunk.steady.vertices }, .indices = { chunk.steady.indices }, .index_type = render::IndexType::U32, .count = batch.count, .first = batch.first, .transform = transform };
                        draw.texture = flow->second.handle;
                        draw.filter = render::Filter::Linear;
                        draw.address = render::AddressMode::Repeat;
                        draw.light = light;
                        draw.extra_textures = { render::TextureHandle { }, render::TextureHandle { }, render::TextureHandle { }, scene.water.ripple_texture };
                        draw.water = { .eye = parts.eye, .time = scene.park_clock, .near = rct3::park::kNear, .far = rct3::park::kFar, .waves = scene.water.look.waves, .flow = true };
                        draw.v_shift = rct3::park::water::flow_scroll(scene.park_clock, batch.chain);
                        device.draw(draw);
                    }
                });
            if (parts.see_through && (parts.water || parts.mirrored))
                profiling::section("Waterfalls", [&] {
                    // The main view has the scene behind the water copied; from under a lake only the sheet shows,
                    // through its surface.
                    FallsSeen seen = parts.mirrored ? FallsSeen::Mirrored : FallsSeen::Direct;
                    if (parts.shaded_water)
                        seen = scene.water.eye_in ? FallsSeen::FromUnderWater : FallsSeen::WithScene;
                    draw_park_waterfalls(device, scene.waterfalls, scene.water, transform, light, parts.eye, scene.park_clock, seen);
                });
            if (parts.water && parts.see_through)
                profiling::section("Jets", [&] {
                    // As draw_transient, each picture with its second (park/water/jet.hpp).
                    const rct3::park::object::ObjectMesh& mesh = scene.jet_mesh;
                    if (mesh.indices.empty())
                        return;
                    const std::optional<FrameUpload> uploaded = upload_once(device, scene.frame_uploads, mesh);
                    if (!uploaded)
                        return;
                    for (const rct3::park::object::ObjectBatch& batch : mesh.batches)
                    {
                        const auto picture = std::ranges::find(rct3::park::water::kJetPictures, batch.texture, &rct3::park::water::JetPicture::key);
                        const auto texture = scene.object_textures.find(batch.texture);
                        const auto layer = scene.object_textures.find(picture->layer_key);
                        render::DrawCommand draw { .pipeline = scene.jet_pipeline, .vertices = uploaded->vertices, .indices = uploaded->indices, .index_type = render::IndexType::U32, .count = batch.count, .first = batch.first, .transform = transform };
                        draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
                        // Without its second picture, drawn alone.
                        if (layer != scene.object_textures.end())
                            draw.extra_textures[0] = layer->second.handle;
                        else
                            draw.pipeline = scene.see_through_pipeline;
                        draw.address = render::AddressMode::Repeat;
                        draw.light = light;
                        draw.v_shift = rct3::park::water::jet_layer_scroll(*picture, scene.park_clock);
                        device.draw(draw);
                    }
                });
            if (scene.show_scenery && parts.scenery && parts.see_through)
                profiling::section("SeeThrough", [&] { draw_objects(rct3::park::object::PartBlend::Blended); });
            if (parts.ghosts)
            {
                track::draw_track_ghost(device, scene, transform, light);
                draw_raised_footpath_ghost(device, scene, transform, light);
                draw_ride_placing_ghost(device, scene, transform, light);
                draw_structure_ghost(device, scene, transform, light);
                draw_pool_ghost(device, scene, transform, light);
            }
            // Last, so what it hides is drawn first and shows through it.
            if (parts.terrain && parts.faded_ground)
                profiling::section("FadedGround", [&] {
                    draw_ground(scene.ground_depth_pipeline, earth);
                    draw_ground(scene.ground_faded_pipeline, earth);
                });
        }

        /// The shadow maps' size: half on the software backend, where filling them costs most of a frame.
        u32 shadow_texels(const render::Device& device)
        {
            return device.backend() == render::Backend::Software ? 1024 : 2048;
        }
        constexpr f32 kShadowSunHeight = 0.02f; ///< Of the sun's way, how far up it is before things cast shadows

        /// Makes `target` for a shadow map when it is not made yet; false when the device cannot.
        bool make_shadow_target(render::Device& device, render::TextureHandle& target)
        {
            if (target)
                return true;
            auto made = device.create_texture({ .width = shadow_texels(device), .height = shadow_texels(device), .label = "shadow map", .target = true, .readable_depth = true });
            if (!made)
                return false;
            target = *made;
            return true;
        }

        /// Draws `casters` of the park round the camera, as `sun` sees them, into the depth of `into.target`,
        /// outside any pass.
        void draw_shadow(render::Device& device, ParkScene& scene, const render::PassDesc& into, const glm::mat4& sun, Casters casters)
        {
            device.begin_pass(into);
            // Without shadows: the map would be read while drawn into.
            render::SceneLight light = scene.light;
            light.shadow_map = light.shadow_map_before = { };
            light.shadow_view = { };
            light.lamps = { };
            light.depth_only = true; // Only the map's depth is read
            draw_world(device, scene, sun, light, { .background = false, .water = false, .see_through = false, .casters = casters });
            device.end_pass();
        }

        /// Draws the park round the camera from the sun into the shadow maps, and gives `scene.light` them;
        /// without shadows, or with the sun down, takes them away. Cut-out pictures leave their clear pixels out
        /// of the maps, so leaves and bushes cast their own shapes.
        ///
        /// The sun is drawn from only now and then, when it has gone half a degree, and the drawing before is
        /// kept: the shader mixes the two by how far the sun is on its way to the next step, so the shadow
        /// moves smoothly yet each map is drawn from a steady direction, without the crawl of one drawn anew
        /// each frame. It lags the sun by a step.
        ///
        /// What stands still is drawn into each drawing's own map and kept until the sun steps, the view leaves
        /// its step (rct3::park::sun_view), or the park changes; what moves is drawn over a copy of each.
        void draw_shadow_map(render::Device& device, ParkScene& scene)
        {
            scene.light.shadow_map = { };
            scene.light.shadow_view = { };
            scene.light.shadow_map_before = { };
            scene.light.shadow_before = 0;
            if (!scene.shadows || scene.light.sun_direction.y <= kShadowSunHeight)
                return;
            if (!make_shadow_target(device, scene.shadow_map) || !make_shadow_target(device, scene.shadow_drawings[0].still) || !make_shadow_target(device, scene.shadow_drawings[1].still) || (scene.blend_shadows && !make_shadow_target(device, scene.shadow_map_before)))
            {
                scene.shadows = 0;
                return;
            }
            if (!scene.shadow_views)
            {
                const std::vector<std::byte> blank(5 * 2 * 16);
                auto made = device.create_texture({ .width = 5, .height = 2, .format = render::TextureFormat::Rgba32Float, .pixels = blank, .label = "shadow views" });
                if (!made)
                {
                    scene.shadows = 0;
                    return;
                }
                scene.shadow_views = *made;
            }
            const f32 step = glm::radians(0.5f);
            const glm::vec3 to_sun = glm::normalize(scene.light.sun_direction);
            ShadowDrawing* now = &scene.shadow_drawings[scene.shadow_now];
            const bool stepped = rct3::park::steady_sun(now->sun, to_sun, step) != now->sun;
            if (stepped)
            {
                scene.shadow_now = 1 - scene.shadow_now;
                now = &scene.shadow_drawings[scene.shadow_now];
                now->sun = to_sun;
            }
            ShadowDrawing& before = scene.shadow_drawings[1 - scene.shadow_now];
            const bool blend = scene.blend_shadows && before.drawn != glm::mat4(0.0f);
            // Riding along, round the rider, as far as an overhead view from a middling height shadows.
            constexpr f32 kRideShadowDistance = 60;
            const f32 centre_radius = rct3::park::shadow_radius(scene.seat ? kRideShadowDistance : scene.camera.distance);
            const glm::vec3 centre = scene.seat ? glm::vec3((*scene.seat)[3]) : scene.camera.target;
            const rct3::park::SunView sun = rct3::park::sun_view(now->sun, centre, centre_radius, shadow_texels(device));
            const rct3::park::SunView sun_before = rct3::park::sun_view(before.sun, centre, centre_radius, shadow_texels(device));
            // Every two seconds the latest still drawing is drawn again anyway, in case a change slipped past
            // still_changes; static shadows then show where things moved to.
            const bool timed = scene.clock - scene.shadow_drawn_at > 2;
            const auto stale = [&](const ShadowDrawing& drawing, const rct3::park::SunView& view) { return drawing.drawn != view.view_projection || drawing.changes != scene.still_changes; };
            const bool still_now = timed || stepped || stale(*now, sun);
            const bool still_before = blend && stale(before, sun_before);
            // Static shadows draw what moves only with the still drawing under it, dynamic ones every frame.
            const bool moving_now = scene.shadows > 1 || still_now;
            scene.light.shadow_map = device.depth_texture(scene.shadow_map);
            scene.light.shadow_view = scene.shadow_views;
            // What moves is in both maps: in the latest alone, its shadow would vanish at each step of the sun
            // and fade back in as the step's share grows, a blink every second or so.
            const bool moving_before = blend && (moving_now || still_before);
            if (blend)
            {
                scene.light.shadow_map_before = device.depth_texture(scene.shadow_map_before);
                scene.light.shadow_before = 1 - rct3::park::sun_step_share(now->sun, to_sun, step);
            }
            if (!moving_now && !moving_before)
                return;
            if (timed)
                scene.shadow_drawn_at = scene.clock;
            device.end_pass();
            const auto draw_still = [&](ShadowDrawing& drawing, const rct3::park::SunView& view) {
                // The depth bias: about a tenth of a world unit, in the map's depth.
                const glm::mat4& m = view.view_projection;
                drawing.view = { m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3], view.texel.value, 0.1f / (2 * rct3::park::kSunViewDepth), 0, 0 };
                drawing.drawn = m;
                drawing.changes = scene.still_changes;
                draw_shadow(device, scene, { .target = drawing.still }, m, Casters::Still);
            };
            if (still_now)
                profiling::section("Now", [&] { draw_still(*now, sun); });
            if (still_before)
                profiling::section("Before", [&] { draw_still(before, sun_before); });
            if (moving_now)
                profiling::section("Moving", [&] { draw_shadow(device, scene, { .target = scene.shadow_map, .depth_from = now->still }, sun.view_projection, Casters::Moving); });
            if (moving_before)
                profiling::section("MovingBefore", [&] { draw_shadow(device, scene, { .target = scene.shadow_map_before, .depth_from = before.still }, sun_before.view_projection, Casters::Moving); });
            // A step swaps which drawing is the latest, and it is always drawn then, so the rows follow here.
            std::array<f32, 40> views { };
            std::ranges::copy(now->view, views.begin());
            std::ranges::copy(before.view, views.begin() + 20);
            device.update_texture(scene.shadow_views, std::as_bytes(std::span(views)));
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

        /// Draws into the round view of `panel`, by `draw` given the view's transform: into `target`, made again
        /// when the view's size in pixels (`pixels`) changes, cleared to `back`.
        template<typename Panel, typename Draw> void draw_view(render::Device& device, ParkScene& scene, Panel& panel, render::TextureHandle& target, u32& pixels, std::string_view label, render::Color back, Draw&& draw)
        {
            const f32 scale = scale_for(device, scene);
            const auto wanted = static_cast<u32>(std::lround(ui::screens::info::info_panel::kView * scale));
            if (target && pixels != wanted)
            {
                device.destroy_texture(target);
                target = { };
            }
            if (!target && wanted > 0)
            {
                render::TextureDesc desc;
                desc.width = wanted;
                desc.height = wanted;
                desc.target = true;
                desc.label = label;
                if (auto made = device.create_texture(desc))
                {
                    target = *made;
                    pixels = wanted;
                }
            }
            if (!target)
                return;
            device.end_pass();
            render::PassDesc into;
            into.target = target;
            into.clear_color = back;
            device.begin_pass(into);
            draw();
            device.end_pass();
            render::PassDesc rest;
            rest.clear = false;
            device.begin_pass(rest);
            panel.show_view(ui::texture_id(target));
        }

        constexpr render::Color kPeopleViewBack = render::hex("#f2f2f2"); ///< Near white behind people shown alone

        /// Draws the people `shown` (of the shown people) as they are now, alone in a row at the middle of the
        /// world, seen from a view turning round them as the build list's previews turn.
        void draw_people_alone(render::Device& device, const ParkScene& scene, std::span<const usize> shown)
        {
            if (!scene.person_models)
                return;
            constexpr f32 kGap = 0.7f;    ///< Between neighbours in the row
            constexpr f32 kHeight = 2.0f; ///< Of the box the view fits, a tall adult's and a little over
            const f32 first = -0.5f * kGap * static_cast<f32>(shown.size() - 1);
            std::vector<rct3::park::person::PlacedPerson> placed;
            for (usize k = 0; k < shown.size(); ++k)
            {
                if (shown[k] >= scene.shown_people.size())
                    continue;
                // Seated on a ride they stand idle instead.
                rct3::park::person::PlacedPerson pose = pose_person(scene, shown[k], scene.people_clock, scene.simulate).value_or(rct3::park::person::PlacedPerson { .look = scene.shown_people[shown[k]].pose.look, .motion = rct3::park::person::motion::kIdle, .time = scene.clock });
                pose.level = 0;
                pose.world = glm::translate(glm::mat4(1.0f), glm::vec3(first + kGap * static_cast<f32>(k), 0, 0));
                placed.push_back(pose);
            }
            const std::array<glm::vec3, 2> box { glm::vec3(first - kGap / 2, 0, -kGap / 2), glm::vec3(kGap / 2 - first, kHeight, kGap / 2) };
            const rct3::park::ParkCamera camera = rct3::park::framed(box, kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
            // shortcut: posed and built afresh each frame the panel shows, a few small allocations; keep the lists in
            // the scene if it ever shows in a profile.
            rct3::park::person::PersonRuns runs;
            const rct3::park::object::ObjectMesh mesh = rct3::park::person::person_mesh(*scene.person_models, placed, runs);
            // Lit by the noon sun whatever the park's hour, so they look the same by night; no shadows, lamps or haze.
            constexpr f32 kNoon = 0.5f; ///< Of a day from midnight
            const rct3::park::Daylight noon = rct3::park::daylight(kNoon);
            const render::SceneLight light { .sun_direction = noon.sun_direction, .sun = noon.sun, .sky = noon.sky, .ground = noon.ground };
            draw_transient(device, scene, std::nullopt, mesh, scene.people_textures, rct3::park::view_projection(camera, 1), light, [&](const rct3::park::object::ObjectBatch&) { return scene.object_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
        }

        /// Draws the first photo waiting into the photo target and asks for its pixels, for the app to save.
        void draw_photo(render::Device& device, ParkScene& scene)
        {
            const PhotoShot shot = std::move(scene.photos.front());
            scene.photos.erase(scene.photos.begin());
            if (!scene.photo_target)
            {
                render::TextureDesc desc;
                desc.width = kPhotoWidth;
                desc.height = kPhotoHeight;
                desc.target = true;
                desc.label = "photo";
                if (auto made = device.create_texture(desc))
                    scene.photo_target = *made;
            }
            if (!scene.photo_target)
                return;
            device.end_pass();
            render::PassDesc into;
            into.target = scene.photo_target;
            into.clear_color = scene.horizon;
            device.begin_pass(into);
            const glm::mat4 transform = rct3::park::view_projection(shot.camera, static_cast<f32>(kPhotoWidth) / kPhotoHeight);
            const glm::vec3 eye = rct3::park::eye(shot.camera);
            draw_world(device, scene, transform, scene.light, { .guests = false, .eye = eye });
            if (scene.show_people)
            {
                const rct3::park::object::ObjectMesh people = photo_people(scene, eye, transform);
                draw_transient(device, scene, std::nullopt, people, scene.people_textures, transform, scene.light, [&](const rct3::park::object::ObjectBatch&) { return scene.object_pipeline; }, [](const rct3::park::object::ObjectBatch&) { return render::AddressMode::Clamp; });
            }
            device.end_pass();
            if (device.read_screen(scene.photo_target))
                scene.photo_reading = shot.name;
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

    } // namespace

    f32 park_interface_scale(const render::Device& device, const ParkScene& scene)
    {
        return scale_for(device, scene);
    }

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool interface)
    {
        scene.frame_uploads.clear();
        // The ripples on the still water of objects (fountain basins, rct3::park::kWaterPicture) move on to now.
        if (const auto texture = scene.object_textures.find(rct3::park::object::kWaterPicture); texture != scene.object_textures.end())
        {
            constexpr f32 kRate = 12; ///< Chosen by eye: pictures a second, smooth enough for slow ripples
            if (const auto frame = static_cast<u32>(scene.park_clock * kRate); frame != scene.ripple_frame)
            {
                scene.ripple_frame = frame;
                const rct3::park::PaletteImage picture = rct3::park::object::water_picture(static_cast<f32>(frame) / kRate);
                // Halved as it was when first uploaded, to fit the texture.
                const ovl::texture::RgbaImage rippled = ovl::texture::halved(rct3::park::paint(picture, { }), ovl::texture::texture_halvings(scene.texture_detail, picture.width, picture.height));
                device.end_pass();
                device.update_texture(texture->second.handle, std::as_bytes(std::span(rippled.pixels)));
                render::PassDesc back;
                back.clear = false;
                device.begin_pass(back);
            }
        }
        // Off each pool slide's open end its water falls, from its lip onto the water or ground below; the
        // spray and rings it makes are set off with the cars' splashes (scene/park/refresh.cpp).
        // shortcut: found again only when the park's count of objects changes, upgrade if a moved end piece keeps
        // its old pour.
        if (scene.pours_found != scene.objects.size())
        {
            scene.pours_found = scene.objects.size();
            scene.slide_pours.clear();
            scene.pours_next = 0;
            constexpr units::Pace kPourSpeed { 1.5f }; ///< The water leaves the lip at. Chosen by eye
            for (const save::model::PlacedObject& o : scene.objects)
            {
                const auto model = scene.models.find(rct3::park::object::model_ref(o));
                if (o.hidden || model == scene.models.end() || !model->second.pour)
                    continue;
                const rct3::park::object::SlidePour& pour = *model->second.pour;
                const glm::mat4 place = rct3::park::object::placed_transform(o, &model->second, scene.grid, scene.fitted);
                const glm::vec3 left(place * glm::vec4(pour.left, 1));
                const glm::vec3 right(place * glm::vec4(pour.right, 1));
                const glm::vec3 way(place * glm::vec4(pour.way, 0));
                const glm::vec2 level = glm::vec2(way.x, way.z);
                if (glm::length(level) < 1e-3f)
                    continue;
                const glm::vec2 flow = glm::normalize(level);
                const glm::vec3 lip = (left + right) / 2.0f;
                // Down onto the water under the lip, or the ground with none there.
                f32 below = scene.grid.height_at(lip.x, lip.z).value_or(lip.y);
                for (f32 y = lip.y; y > below - 1; y -= 0.25f)
                    if (const auto water = rct3::park::water::water_above(scene.water.surface, { lip.x, y, lip.z }))
                    {
                        below = water->level;
                        break;
                    }
                if (below >= lip.y)
                    continue;
                const f32 throw_out = kPourSpeed.value * std::sqrt(2 * (lip.y - below) / units::kGravity.value);
                scene.slide_pours.push_back({ .left = left, .right = right, .landing = { lip.x + flow.x * throw_out, below, lip.z + flow.y * throw_out }, .water = below > scene.grid.height_at(lip.x, lip.z).value_or(below) });
            }
        }
        if (!scene.jets.empty() || !scene.jet_mesh.indices.empty())
            profiling::section("Jets", [&] {
                rct3::park::object::ObjectMesh& mesh = scene.jet_mesh;
                mesh.vertices.clear();
                mesh.indices.clear();
                mesh.batches.clear();
                mesh.owners.clear();
                std::vector<const rct3::park::water::WaterJet*> jets;
                for (const ParkScene::Jet& jet : scene.jets)
                    jets.push_back(&jet.water);
                rct3::park::water::jet_mesh(jets, rct3::park::eye(scene.camera, scene.seat), scene.park_clock, mesh);
            });
        profiling::section("LedSigns", [&] { roll_led_signs(device, scene); });
        // A person shows alone on a light ground, turning as the build list's pieces do; the rest in the park.
        if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && (scene.shown || scene.shown_person || scene.shown_prop || scene.shown_animal || !scene.shown_party.empty()))
        {
            // A person, or a party, stands alone; anything else is seen where it is in the park.
            const std::array person { scene.shown_person.value_or(0) };
            const std::span<const usize> alone = scene.shown_person ? std::span<const usize>(person) : std::span<const usize>(scene.shown_party);
            profiling::section("InfoView", [&] { draw_view(device, scene, *info, scene.view_target, scene.view_pixels, "attraction view", !alone.empty() ? kPeopleViewBack : scene.horizon, [&] { !alone.empty() ? draw_people_alone(device, scene, alone) : draw_world(device, scene, rct3::park::view_projection(scene.view_camera, 1), scene.light, { .eye = rct3::park::eye(scene.view_camera) }); }); });
        }
        // The item being placed, alone on the disc's grey, lit as the park is.
        // shortcut: its see-through parts are drawn in the mesh's order, not after the rest; sort them if a
        // preview shows a part hidden behind glass.
        if (auto pools = scene.desktop->root().find_as<ui::screens::build::PoolPanel>("pool_panel"); pools && pools->showing() && scene.pooling.item && !scene.pooling.preview.indices.empty())
            draw_view(device, scene, *pools, scene.pooling.preview_target, scene.pooling.preview_pixels, "pool view", render::hex("#7f7f7f"), [&] { draw_ghost_mesh(device, scene, scene.pooling.preview, rct3::park::view_projection(scene.pooling.preview_camera, 1), scene.light, true); });
        if (auto placing = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kPlacingName); placing && placing->showing() && !scene.placing.preview_mesh.indices.empty())
            profiling::section("PlacingView", [&] { draw_view(device, scene, *placing, scene.placing.preview_target, scene.placing.preview_pixels, "placing view", render::hex("#7f7f7f"), [&] { draw_ghost_mesh(device, scene, scene.placing.preview_bones.empty() ? scene.placing.preview_mesh : scene.placing.preview_moving.mesh, rct3::park::view_projection(scene.placing.preview_camera, 1), scene.light, true); }); });
        if (!scene.photos.empty() && !scene.photo_reading)
            profiling::section("Photo", [&] { draw_photo(device, scene); });
        const glm::mat4 view_projection = rct3::park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across);
        const glm::vec3 eye = rct3::park::eye(scene.camera, scene.seat);
        profiling::section("Shadows", [&] { draw_shadow_map(device, scene); });
        profiling::section("Lamps", [&] {
            // Lamps come on as the sky darkens, packed again when the strength has moved enough to see.
            scene.light.lamps = { };
            constexpr f32 kLampStrength = 0.75f; // Path lamps light their pools without washing them white
            const f32 strength = scene.darkness * kLampStrength;
            // Glowing effects set off once (firework bursts, explosions) light what is round them as their sparks
            // do: bright as they burst, fading as the sparks fade and die.
            constexpr usize kMostBursts = 8;               // Most lit at once, the brightest: each lights many cells
            constexpr f32 kBurstLight = 0.002f;            // Chosen by eye: a big burst's thousand sparks light the ground under it like a lamp
            constexpr units::Distance kBurstReach { 120 }; // Chosen by eye: from the highest shells, down to the ground and paths round the launchers
            std::vector<rct3::park::Lamp>& bursts = scene.bursts;
            bursts.clear();
            if (scene.darkness >= 0.01f)
                for (const ParkParticles::Shown& shown : scene.particles.fired)
                    if (shown.awake)
                        if (const particles::Glow glow = particles::glow(shown.run.effect(), shown.run.particles(), shown.sprites); glm::length(glow.colour) > 0)
                            bursts.push_back({ .position = glow.position, .radius = kBurstReach.value, .colour = glow.colour * kBurstLight * scene.darkness });
            rct3::park::keep_brightest(bursts, kMostBursts);
            if (strength < 0.01f || (scene.lamps.empty() && !scene.car_lamps && bursts.empty()))
                return;
            if (scene.lamps_due || scene.car_lamps || !bursts.empty() || !scene.lamp_strength || std::abs(strength - *scene.lamp_strength) > 0.01f)
            {
                std::vector<rct3::park::Lamp> lamps = scene.lamps;
                if (scene.car_lamps)
                    std::ranges::copy(rct3::park::object::object_lamps(scene.cars, scene.models, scene.grid, { }, scene.car_places), std::back_inserter(lamps));
                for (rct3::park::Lamp& l : lamps)
                    l.colour *= strength;
                // After the lamps, so a cell crowded with lamps keeps them all and leaves the bursts out there.
                std::ranges::copy(bursts, std::back_inserter(lamps));
                if (lamps.empty())
                    return;
                constexpr f32 kCell = 8; // Two tiles, a lamp's usual reach
                rct3::park::LampTexels texels = rct3::park::lamp_texels(lamps, kCell);
                // A texture with rows to spare is kept and the rest padded, so bursts moving the grid each frame
                // do not make a new one each frame.
                if (scene.lamp_texture && scene.lamp_rows < texels.rows)
                {
                    device.destroy_texture(scene.lamp_texture);
                    scene.lamp_texture = { };
                }
                if (scene.lamp_texture)
                    texels.values.resize(usize { scene.lamp_rows } * rct3::park::kLampTexelsAcross * 4, -1.0f);
                const auto bytes = std::as_bytes(std::span(texels.values));
                if (!scene.lamp_texture)
                {
                    auto made = device.create_texture({ .width = rct3::park::kLampTexelsAcross, .height = texels.rows, .format = render::TextureFormat::Rgba32Float, .pixels = bytes, .label = "lamps" });
                    if (!made)
                        return;
                    scene.lamp_texture = *made;
                    scene.lamp_rows = texels.rows;
                }
                else
                {
                    device.end_pass();
                    device.update_texture(scene.lamp_texture, bytes);
                    render::PassDesc back;
                    back.clear = false;
                    device.begin_pass(back);
                }
                // Packed again next frame while bursts glow, and once more as the last fades.
                scene.lamps_due = !bursts.empty();
                scene.lamp_strength = strength;
            }
            scene.light.lamps = scene.lamp_texture;
        });
        profiling::section("Reflections", [&] {
            const WaterLook& look = scene.water.look;
            // Seen from the eye mirrored in the water's level, as the transform mirrors the world.
            prepare_park_water(device, scene.water, view_projection, eye, scene.horizon, [&](const glm::mat4& transform, f32 level) {
                draw_world(device, scene, transform, scene.light, { .background = look.reflect_background, .terrain = look.reflect_terrain, .scenery = look.reflect_scenery, .people = look.reflect_people, .animals = look.reflect_animals, .water = false, .mirrored = true, .eye = { eye.x, 2 * level - eye.y, eye.z } });
                // Bursts and fire shine in the water too.
                draw_park_particles(device, scene.particles, transform, { eye.x, 2 * level - eye.y, eye.z }, { scene.camera.target.x, 2 * level - scene.camera.target.y, scene.camera.target.z }, scene.light);
            });
        });
        profiling::section("World", [&] {
            device.set_wireframe(scene.wireframe);
            draw_world(device, scene, view_projection, scene.light, { .shaded_water = true, .ghosts = true, .faded_ground = ground_faded(scene), .eye = eye });
            device.set_wireframe(false);
        });
        if (scene.ambient_occlusion)
            profiling::section("Occlusion", [&] { draw_park_occlusion(device, scene.occlusion, view_projection, rct3::park::projection(aspect(device), scene.seat, scene.seat_across)); });
        else
            scene.occlusion.before = false;
        if (scene.show_scenery)
            profiling::section("Particles", [&] { draw_park_particles(device, scene.particles, view_projection, eye, scene.camera.target, scene.light); });
        if (scene.weather_look.rain > 0 && !scene.water.eye_in)
        {
            const f32 lit = std::max({ scene.light.sky.r, scene.light.sky.g, scene.light.sky.b });
            profiling::section("Rain", [&] { draw_park_rain(device, scene.rain, { .view_projection = view_projection, .eye = eye, .amount = scene.weather_look.rain, .wind = scene.park.wind, .seconds = scene.clock, .light = lit }); });
        }
        LightShafts shafts;
        if (scene.light_shafts)
        {
            // How strong the shafts are at most. They fade in as the sun rises, and read as far into
            // the air as the shadow map reaches, beyond which nothing shadows it.
            constexpr f32 kShaftStrength = 0.22f;
            const f32 risen = glm::smoothstep(0.0f, 0.1f, glm::normalize(scene.light.sun_direction).y);
            shafts = { .from_clip = glm::inverse(view_projection), .light = scene.light, .reach = rct3::park::shadow_radius(scene.camera.distance), .strength = kShaftStrength * risen };
        }
        if (scene.bloom || shafts.strength > 0.0f)
            profiling::section("Glow", [&] { draw_park_glow(device, scene.glow, scene.bloom, shafts); });
        if (!scene.lens.water.drops.empty())
            profiling::section("LensDrops", [&] { draw_park_lens(device, scene.lens); });

        if (!interface || !scene.show_interface)
            return;
        const profiling::Scope zone("Interface");
        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::font::FontSet kNoFonts;
        ui::DrawContext context { scene.list, assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts, assets.palette };
        scene.desktop->draw(context);
        ui::render_draw_list(device, assets.renderer, scene.list);
    }

} // namespace rct3::scene::park
