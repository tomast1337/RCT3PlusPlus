// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/chain_lifts.hpp"
#include "park/ground_textures.hpp"
#include "park/object_models.hpp"
#include "park/sun_view.hpp"
#include "park/water_flow.hpp"
#include "park/waterfall_mesh.hpp"
#include "profiling/profiler.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_glow.hpp"
#include "scene/park_occlusion.hpp"
#include "scene/park_patrol.hpp"
#include "scene/park_refresh.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "scene/park_water.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <span>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        // Size of photos at photo spots.
        constexpr std::uint32_t kPhotoWidth = 640;
        constexpr std::uint32_t kPhotoHeight = 480;

        /// Draws `mesh` from buffers made for this frame, each batch with `pipeline_of(batch)`; with its vertices
        /// as `vertex_bytes` when given, for pipelines of another layout.
        void draw_transient(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const ui::UiTextures& textures, const glm::mat4& transform, const render::SceneLight& light, auto&& pipeline_of, auto&& address_of, std::span<const std::byte> vertex_bytes = { })
        {
            if (mesh.indices.empty())
                return;
            const auto vertices = device.allocate_transient(vertex_bytes.empty() ? std::as_bytes(std::span(mesh.vertices)) : vertex_bytes);
            const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
            if (!vertices || !indices)
                return;
            for (const park::ObjectBatch& batch : mesh.batches)
            {
                const auto texture = textures.find(batch.texture);
                render::DrawCommand draw;
                draw.pipeline = pipeline_of(batch);
                draw.vertices = *vertices;
                draw.indices = *indices;
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

        /// Which parts of the world a draw shows: a reflection leaves some out, as the options ask.
        struct WorldParts
        {
            bool background = true; ///< Sky and mountains
            bool terrain = true;
            bool scenery = true;
            bool people = true;
            bool water = true;
            bool see_through = true;   ///< Blended parts, such as glass
            bool shaded_water = false; ///< With the water shader, seen from `eye`
            bool ghosts = false;       ///< The track and raised path being built
            bool faded_ground = false; ///< The ground's land, cliffs and tunnels drawn last, see-through
            glm::vec3 eye { 0.0f };
        };

        /// The park's ground, objects, water, sky and mountains as `transform` sees them, lit by `light`.
        void draw_world(render::Device& device, ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light, const WorldParts& parts = { })
        {
            if (scene.sky_pipeline && parts.background)
            {
                const profiling::Scope zone("Sky");
                // The dome reads one column of its picture, so it must not wrap into the next.
                draw_transient(device, scene, scene.sky_mesh, scene.sky_textures, transform, light, [&](const park::ObjectBatch&) { return scene.sky_pipeline; }, [&](const park::ObjectBatch& b) { return b.texture == scene.sky.dome.texture ? render::AddressMode::Clamp : render::AddressMode::Repeat; });
            }
            // Before the ground, writing depth, so land reaching beyond the ring stays hidden behind it.
            if (parts.background)
                profiling::section("Mountains", [&] { draw_transient(device, scene, scene.mountains, scene.sky_textures, transform, light, [&](const park::ObjectBatch&) { return scene.cutout_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; }); });
            const auto pipeline_for = [&](const ShownObjects& shown, park::PartBlend blend) {
                const bool crossing = &shown != &scene.steady_objects;
                if (blend == park::PartBlend::Opaque)
                    return crossing ? scene.crossing_pipeline : scene.object_pipeline;
                if (blend == park::PartBlend::Cutout)
                    return crossing ? scene.crossing_cutout_pipeline : scene.cutout_pipeline;
                return crossing ? scene.crossing_see_through_pipeline : scene.see_through_pipeline;
            };
            const auto sided = [&](render::PipelineHandle pipeline, const park::ObjectBatch& batch) { return batch.one_sided ? scene.front_only.at(pipeline.id) : pipeline; };
            const auto object_pipeline = [&](const park::ObjectBatch& b) { return sided(pipeline_for(scene.steady_objects, b.blend), b); };
            const auto draw_shown = [&](const ShownObjects& shown, bool see_through) {
                if (!shown.indices)
                    return;
                for (const park::ObjectBatch& batch : shown.batches)
                {
                    if ((batch.blend == park::PartBlend::Blended) != see_through)
                        continue;
                    // Parts painted like the ground take the ground's pictures (see object_mesh.hpp).
                    const ui::UiTextures& textures = scene.object_textures.contains(batch.texture) ? scene.object_textures : scene.ground_textures;
                    const auto texture = textures.find(batch.texture);
                    render::DrawCommand draw;
                    draw.pipeline = sided(pipeline_for(shown, batch.blend), batch);
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
                    draw.scroll = park::chain_scroll(scene.chain_shifts, batch.chain);
                    device.draw(draw);
                }
            };
            // Land, cliffs and tunnels, as against the paths and queues drawn on the ground.
            const auto earth = [](const park::GroundBatch& batch) { return batch.paint == park::GroundPaint::Surface || batch.paint == park::GroundPaint::Cliff || batch.paint == park::GroundPaint::TunnelWall || batch.paint == park::GroundPaint::TunnelCeiling; };
            const auto draw_ground = [&](render::PipelineHandle pipeline, auto&& wanted) {
                for (const park::GroundBatch& batch : scene.batches)
                {
                    if (!wanted(batch))
                        continue;
                    const auto texture = scene.ground_textures.find(park::ground_key(batch.paint, batch.index));
                    render::DrawCommand draw;
                    draw.pipeline = pipeline;
                    draw.vertices = { scene.ground_vertices };
                    draw.indices = { scene.ground_indices };
                    draw.index_type = render::IndexType::U32;
                    draw.first = batch.first;
                    draw.count = batch.count;
                    draw.transform = transform;
                    draw.texture = texture != scene.ground_textures.end() ? texture->second.handle : scene.blank;
                    draw.filter = render::Filter::Linear;
                    draw.address = render::AddressMode::Repeat;
                    draw.light = light;
                    draw.extra_textures[0] = scene.variation;
                    draw.extra_textures[1] = relief_of(scene.ground_textures, park::ground_key(batch.paint, batch.index));
                    device.draw(draw);
                }
            };
            if (parts.terrain)
                profiling::section("Ground", [&] {
                    draw_ground(scene.ground_pipeline, [&](const park::GroundBatch& batch) { return !parts.faded_ground || !earth(batch); });
                    draw_view_lines(device, scene, transform, light);
                    draw_footpath_ghost(device, scene, transform, light);
                    draw_patrol(device, scene, transform, light);
                });
            const auto draw_objects = [&](bool see_through) {
                for (const ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                    draw_shown(*shown, see_through);
            };
            if (scene.show_scenery && parts.scenery)
                profiling::section("Objects", [&] {
                    draw_objects(false);
                    draw_transient(device, scene, scene.fresh_objects, scene.object_textures, transform, light, object_pipeline, [](const park::ObjectBatch&) { return render::AddressMode::Repeat; });
                });
            if (scene.show_scenery && parts.scenery)
                profiling::section("Billboards", [&] {
                    for (const ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                    {
                        // shortcut: crossing billboards are copied with their bands each frame; fine for the
                        // few in bands, upgrade to a second vertex stream if many trees cross at once.
                        const std::vector<CrossingVertex> crossing = crossing_vertices(shown->billboards.mesh.vertices, shown->bands);
                        draw_transient(device, scene, shown->billboards.mesh, scene.object_textures, transform, light, [&](const park::ObjectBatch& b) { return sided(pipeline_for(*shown, b.blend), b); }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; }, std::as_bytes(std::span(crossing)));
                    }
                });
            if (scene.show_people && parts.people)
                profiling::section("People", [&] {
                    draw_transient(device, scene, scene.still_people, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                    draw_transient(device, scene, scene.moving_people, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                    if (scene.simulate)
                        draw_transient(device, scene, scene.riders.mesh, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                });
            if (scene.show_scenery && parts.scenery)
                profiling::section("LedSigns", [&] { draw_led_signs(device, scene, transform, light); });
            if (scene.show_scenery && parts.scenery)
                profiling::section("Cars", [&] { draw_transient(device, scene, scene.car_meshes.mesh, scene.object_textures, transform, light, object_pipeline, [](const park::ObjectBatch&) { return render::AddressMode::Repeat; }); });
            profiling::section("Rides", [&] {
                for (const ParkScene::BonedRide& boned : scene.boned_rides)
                    if ((boned.ride != entt::null && !boned.gate) || maybe_in_view(transform, glm::vec3(boned.placed[3]), boned.reach))
                        draw_transient(device, scene, boned.mesh.mesh, scene.object_textures, transform, light, object_pipeline, [](const park::ObjectBatch&) { return render::AddressMode::Repeat; });
            });
            if (parts.water)
                profiling::section("Water", [&] { draw_park_water(device, scene.water, transform, light, parts.eye, scene.park_clock, parts.shaded_water); });
            if (parts.water && parts.see_through)
                profiling::section("Waterfalls", [&] {
                    // As draw_transient, each picture with its second (park/waterfall_mesh.hpp).
                    const park::ObjectMesh& mesh = scene.waterfall_mesh;
                    if (mesh.indices.empty())
                        return;
                    const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
                    const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
                    if (!vertices || !indices)
                        return;
                    for (const park::ObjectBatch& batch : mesh.batches)
                    {
                        const auto picture = std::ranges::find(park::kWaterfallPictures, batch.texture, &park::WaterfallPicture::key);
                        const auto texture = scene.object_textures.find(batch.texture);
                        const auto layer = scene.object_textures.find(picture->layer_key);
                        render::DrawCommand draw { .pipeline = picture->multiplied ? scene.waterfall_multiplied_pipeline : scene.waterfall_added_pipeline, .vertices = *vertices, .indices = *indices, .index_type = render::IndexType::U32, .count = batch.count, .first = batch.first, .transform = transform };
                        draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
                        // Without its second picture, drawn alone.
                        if (layer != scene.object_textures.end())
                            draw.extra_textures[0] = layer->second.handle;
                        else
                            draw.pipeline = scene.see_through_pipeline;
                        draw.address = render::AddressMode::Repeat;
                        draw.light = light;
                        draw.scroll = park::waterfall_layer_scroll(*picture, scene.park_clock);
                        device.draw(draw);
                    }
                });
            if (scene.show_scenery && parts.scenery && parts.see_through)
                profiling::section("SeeThrough", [&] { draw_objects(true); });
            if (parts.ghosts)
            {
                draw_track_ghost(device, scene, transform, light);
                draw_raised_footpath_ghost(device, scene, transform, light);
                draw_ride_placing_ghost(device, scene, transform, light);
                draw_structure_ghost(device, scene, transform, light);
            }
            // Last, so what it hides is drawn first and shows through it.
            if (parts.terrain && parts.faded_ground)
                profiling::section("FadedGround", [&] {
                    draw_ground(scene.ground_depth_pipeline, earth);
                    draw_ground(scene.ground_faded_pipeline, earth);
                });
        }

        constexpr std::uint32_t kShadowTexels = 2048;

        /// Makes the target of `drawing`; false when the device cannot.
        bool make_shadow_drawing(render::Device& device, ShadowDrawing& drawing)
        {
            auto made = device.create_texture({ .width = kShadowTexels, .height = kShadowTexels, .label = "shadow map", .target = true, .readable_depth = true });
            if (!made)
                return false;
            drawing.target = *made;
            return true;
        }

        /// Rolls the flowing water's picture on as the water flows (park/water_flow.hpp). Call inside the screen
        /// pass; it ends the pass and begins it again keeping what was drawn when the picture moves.
        void roll_flow(render::Device& device, ParkScene& scene)
        {
            const auto texture = scene.object_textures.find(park::kFlowPicture);
            if (texture == scene.object_textures.end())
                return;
            const std::uint32_t shift = park::flow_shift(scene.flow_picture.height, scene.park_clock);
            if (shift == scene.flow_shift)
                return;
            scene.flow_shift = shift;
            const ovl::RgbaImage rolled = park::flow_rows(scene.flow_picture, shift);
            device.end_pass();
            device.update_texture(texture->second.handle, std::as_bytes(std::span(rolled.pixels)));
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

        /// Draws the park round the camera from `drawing.sun` into its map, outside any pass.
        void draw_shadow(render::Device& device, ParkScene& scene, ShadowDrawing& drawing, const park::SunView& sun)
        {
            // The depth bias: about a tenth of a world unit, in the map's depth.
            const std::array<float, 20> values
                = { sun.view_projection[0][0], sun.view_projection[0][1], sun.view_projection[0][2], sun.view_projection[0][3], sun.view_projection[1][0], sun.view_projection[1][1], sun.view_projection[1][2], sun.view_projection[1][3], sun.view_projection[2][0], sun.view_projection[2][1], sun.view_projection[2][2], sun.view_projection[2][3], sun.view_projection[3][0], sun.view_projection[3][1], sun.view_projection[3][2], sun.view_projection[3][3], sun.texel, 0.1f / (2 * park::kSunViewDepth), 0, 0 };
            drawing.drawn = sun.view_projection;
            drawing.view = values;
            render::PassDesc into;
            into.target = drawing.target;
            device.begin_pass(into);
            // Without shadows: the map would be read while drawn into.
            render::SceneLight light = scene.light;
            light.shadow_map = light.shadow_map_before = { };
            light.shadow_view = { };
            light.lamps = { };
            draw_world(device, scene, sun.view_projection, light, { .background = false, .water = false, .see_through = false });
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
        void draw_shadow_map(render::Device& device, ParkScene& scene)
        {
            scene.light.shadow_map = { };
            scene.light.shadow_view = { };
            scene.light.shadow_map_before = { };
            scene.light.shadow_before = 0;
            if (!scene.shadows || scene.light.sun_direction.y <= 0.02f)
                return;
            for (ShadowDrawing& drawing : scene.shadow_drawings)
            {
                if (!drawing.target && !make_shadow_drawing(device, drawing))
                {
                    scene.shadows = 0;
                    return;
                }
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
            const float step = glm::radians(0.5f);
            const glm::vec3 to_sun = glm::normalize(scene.light.sun_direction);
            ShadowDrawing* now = &scene.shadow_drawings[scene.shadow_now];
            const bool stepped = park::steady_sun(now->sun, to_sun, step) != now->sun;
            if (stepped)
            {
                scene.shadow_now = 1 - scene.shadow_now;
                now = &scene.shadow_drawings[scene.shadow_now];
                now->sun = to_sun;
            }
            ShadowDrawing& before = scene.shadow_drawings[1 - scene.shadow_now];
            const bool blend = scene.blend_shadows && before.drawn != glm::mat4(0.0f);
            // Riding along, round the rider, as far as an overhead view from a middling height shadows.
            constexpr float kRideShadowDistance = 60;
            const float centre_radius = park::shadow_radius(scene.seat ? kRideShadowDistance : scene.camera.distance);
            const glm::vec3 centre = scene.seat ? glm::vec3((*scene.seat)[3]) : scene.camera.target;
            const park::SunView sun = park::sun_view(now->sun, centre, centre_radius, kShadowTexels);
            const park::SunView sun_before = park::sun_view(before.sun, centre, centre_radius, kShadowTexels);
            // Static shadows redraw when the view moves, or every two seconds to catch what changed in the park;
            // the older drawing then too, so both cover the same ground and show the same park. A step alone
            // draws only the new one: the older is the one drawn before it.
            // Dynamic shadows draw the newer every frame and the older, fading out, every other frame: a frame
            // behind is not seen, and it saves half the cost of the second drawing.
            const bool timed = scene.clock - scene.shadow_drawn_at > 2;
            const bool dynamic = scene.shadows > 1;
            scene.shadow_before_due = dynamic && !scene.shadow_before_due;
            const bool draw_now = dynamic || timed || stepped || sun.view_projection != now->drawn;
            const bool draw_before = blend && (timed || scene.shadow_before_due || sun_before.view_projection != before.drawn);
            scene.light.shadow_map = device.depth_texture(now->target);
            scene.light.shadow_view = scene.shadow_views;
            if (blend)
            {
                scene.light.shadow_map_before = device.depth_texture(before.target);
                scene.light.shadow_before = 1 - park::sun_step_share(now->sun, to_sun, step);
            }
            if (!draw_now && !draw_before)
                return;
            if (timed)
                scene.shadow_drawn_at = scene.clock;
            device.end_pass();
            if (draw_now)
                profiling::section("Now", [&] { draw_shadow(device, scene, *now, sun); });
            if (draw_before)
                profiling::section("Before", [&] { draw_shadow(device, scene, before, sun_before); });
            // A step swaps which drawing is the latest, and it is always drawn then, so the rows follow here.
            std::array<float, 40> views { };
            std::ranges::copy(now->view, views.begin());
            std::ranges::copy(before.view, views.begin() + 20);
            device.update_texture(scene.shadow_views, std::as_bytes(std::span(views)));
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

        /// Draws into the round view of `panel`, by `draw` given the view's transform: into `target`, made again
        /// when the view's size in pixels (`pixels`) changes, cleared to `back`.
        template<typename Draw> void draw_view(render::Device& device, ParkScene& scene, ui::InfoPanel& panel, render::TextureHandle& target, std::uint32_t& pixels, std::string_view label, render::Color back, Draw&& draw)
        {
            const float scale = scale_for(device, scene);
            const auto wanted = static_cast<std::uint32_t>(std::lround(ui::info_panel::kView * scale));
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
            draw_world(device, scene, park::view_projection(shot.camera, static_cast<float>(kPhotoWidth) / kPhotoHeight), scene.light);
            device.end_pass();
            if (device.read_screen(scene.photo_target))
                scene.photo_reading = shot.name;
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

    } // namespace

    float park_interface_scale(const render::Device& device, const ParkScene& scene)
    {
        return scale_for(device, scene);
    }

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        profiling::section("Flow", [&] { roll_flow(device, scene); });
        if (!scene.footpaths.waterfalls.empty() || !scene.waterfall_mesh.indices.empty())
            profiling::section("Waterfalls", [&] { park::waterfall_mesh(scene.footpaths.waterfalls, scene.park_clock, scene.waterfall_mesh); });
        profiling::section("LedSigns", [&] { roll_led_signs(device, scene); });
        if (auto info = scene.desktop->root().find_as<ui::InfoPanel>(kInfoName); info && (scene.shown || scene.shown_person || scene.shown_prop))
            profiling::section("InfoView", [&] { draw_view(device, scene, *info, scene.view_target, scene.view_pixels, "attraction view", scene.horizon, [&] { draw_world(device, scene, park::view_projection(scene.view_camera, 1), scene.light); }); });
        // The item being placed, alone on the disc's grey, lit as the park is.
        // shortcut: its see-through parts are drawn in the mesh's order, not after the rest; sort them if a
        // preview shows a part hidden behind glass.
        if (auto placing = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName); placing && placing->showing() && !scene.placing.preview_mesh.indices.empty())
            profiling::section("PlacingView", [&] { draw_view(device, scene, *placing, scene.placing.preview_target, scene.placing.preview_pixels, "placing view", render::hex("#7f7f7f"), [&] { draw_ghost_mesh(device, scene, scene.placing.preview_bones.empty() ? scene.placing.preview_mesh : scene.placing.preview_moving.mesh, park::view_projection(scene.placing.preview_camera, 1), scene.light, true); }); });
        if (!scene.photos.empty() && !scene.photo_reading)
            profiling::section("Photo", [&] { draw_photo(device, scene); });
        const glm::mat4 view_projection = park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across);
        const glm::vec3 eye = park::eye(scene.camera, scene.seat);
        profiling::section("Shadows", [&] { draw_shadow_map(device, scene); });
        profiling::section("Lamps", [&] {
            // Lamps come on as the sky darkens, packed again when the strength has moved enough to see.
            scene.light.lamps = { };
            constexpr float kLampStrength = 0.75f; // Path lamps light their pools without washing them white
            const float strength = scene.darkness * kLampStrength;
            if (strength < 0.01f || (scene.lamps.empty() && !scene.car_lamps))
                return;
            if (scene.lamps_due || scene.car_lamps || std::abs(strength - scene.lamp_strength) > 0.01f)
            {
                std::vector<park::Lamp> lamps = scene.lamps;
                if (scene.car_lamps)
                    std::ranges::copy(park::object_lamps(scene.cars, scene.models, scene.grid, { }, scene.car_places), std::back_inserter(lamps));
                if (lamps.empty())
                    return;
                for (park::Lamp& l : lamps)
                    l.colour *= strength;
                constexpr float kCell = 8; // Two tiles, a lamp's usual reach
                const park::LampTexels texels = park::lamp_texels(lamps, kCell);
                if (scene.lamp_texture && scene.lamp_rows != texels.rows)
                {
                    device.destroy_texture(scene.lamp_texture);
                    scene.lamp_texture = { };
                }
                const auto bytes = std::as_bytes(std::span(texels.values));
                if (!scene.lamp_texture)
                {
                    auto made = device.create_texture({ .width = park::kLampTexelsAcross, .height = texels.rows, .format = render::TextureFormat::Rgba32Float, .pixels = bytes, .label = "lamps" });
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
                scene.lamps_due = false;
                scene.lamp_strength = strength;
            }
            scene.light.lamps = scene.lamp_texture;
        });
        profiling::section("Reflections", [&] {
            const WaterLook& look = scene.water.look;
            const WorldParts mirrored { .background = look.reflect_background, .terrain = look.reflect_terrain, .scenery = look.reflect_scenery, .people = look.reflect_people, .water = false };
            prepare_park_water(device, scene.water, view_projection, eye, scene.horizon, [&](const glm::mat4& transform) { draw_world(device, scene, transform, scene.light, mirrored); });
        });
        profiling::section("World", [&] {
            device.set_wireframe(scene.wireframe);
            draw_world(device, scene, view_projection, scene.light, { .shaded_water = true, .ghosts = true, .faded_ground = ground_faded(scene), .eye = eye });
            device.set_wireframe(false);
        });
        if (scene.ambient_occlusion)
            profiling::section("Occlusion", [&] { draw_park_occlusion(device, scene.occlusion, view_projection, park::projection(aspect(device), scene.seat, scene.seat_across)); });
        else
            scene.occlusion.before = false;
        if (scene.show_scenery)
            profiling::section("Particles", [&] { draw_park_particles(device, scene.particles, view_projection, eye, scene.camera.target, scene.light); });
        LightShafts shafts;
        if (scene.light_shafts)
        {
            // How strong the shafts are at most. They fade in as the sun rises, and read as far into
            // the air as the shadow map reaches, beyond which nothing shadows it.
            constexpr float kShaftStrength = 0.22f;
            const float risen = glm::smoothstep(0.0f, 0.1f, glm::normalize(scene.light.sun_direction).y);
            shafts = { .from_clip = glm::inverse(view_projection), .light = scene.light, .reach = park::shadow_radius(scene.camera.distance), .strength = kShaftStrength * risen };
        }
        if (scene.bloom || shafts.strength > 0.0f)
            profiling::section("Glow", [&] { draw_park_glow(device, scene.glow, scene.bloom, shafts); });

        const profiling::Scope zone("Interface");
        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { scene.list, assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts, assets.palette };
        scene.desktop->draw(context);
        ui::render_draw_list(device, assets.renderer, scene.list);
    }

} // namespace rct3::scene
