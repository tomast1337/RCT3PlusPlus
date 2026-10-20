// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/led_signs.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"
#include "park/item_category.hpp"
#include "park/led_signs.hpp"
#include "scene/park_scene.hpp"
#include "strings/ascii.hpp"
#include "ui/font_images.hpp"
#include "ui/font_sets.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/management_pages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>

namespace rct3::scene
{

    namespace
    {
        /// The plain theme's overlay, whose sign rides of no theme take.
        constexpr std::string_view kPlainTheme = "Style/Vanilla/Style";
        /// Stands in for a board's own picture in its model, so its batch can be told apart.
        constexpr std::string_view kBoardPicture = "LED sign board";
        /// The font the boards are written in, from the largest set.
        constexpr std::string_view kBoardFont = "GUIFontScaledLarge";

        std::string forward_slashes(std::string path)
        {
            std::ranges::replace(path, '\\', '/');
            return path;
        }

        /// The overlay of `ride`'s theme: the one it comes from when that is a theme's, else the plain one.
        /// shortcut: rides with a track come from their own folder and take the plain theme's sign; upgrade if
        /// a park shows themed coasters with themed signs.
        std::string theme_of(const save::Ride& ride)
        {
            const std::string path = forward_slashes(ride.base.overlay_path);
            const auto slash = path.rfind('/');
            const bool themed = path.size() > 6 && strings::equals_ignoring_case(path.substr(0, 6), "Style/") && slash != std::string::npos && strings::equals_ignoring_case(path.substr(slash + 1), "Style");
            return themed ? path : std::string(kPlainTheme);
        }

        /// The queue sign of the theme in `overlay`, read once; empty when it has none.
        const std::string& sign_of(LedSigns& q, const ui::InterfaceAssets& assets, const std::string& overlay)
        {
            auto [found, added] = q.sign_of.try_emplace(overlay);
            if (!added || !assets.install)
                return found->second;
            const auto file = assets.install->find(overlay + ".common.ovl");
            const auto read = file ? ovl::Overlay::load(*file) : std::unexpected(ovl::Error { });
            if (!read)
                return found->second;
            std::vector<ovl::SceneryItemEntry> items = ovl::read_scenery_items(*read, read->common());
            if (auto unique = read->unique())
                std::ranges::move(ovl::read_scenery_items(*read, *unique), std::back_inserter(items));
            found->second = park::queue_sign_item(items);
            return found->second;
        }

        /// Paints the board parts of `model` with kBoardPicture, their texture running once along the board, and
        /// returns the board's width over its height; 0 for a model without a board.
        float prepare_board(park::ObjectModel& model)
        {
            float aspect = 0;
            for (park::ModelLevel& level : model.levels)
            {
                for (park::ModelPart& part : level.parts)
                {
                    if (!part.board || part.vertices.empty())
                        continue;
                    glm::vec3 low(std::numeric_limits<float>::max());
                    glm::vec3 high(std::numeric_limits<float>::lowest());
                    float across = 0;
                    for (const park::ModelVertex& v : part.vertices)
                    {
                        low = glm::min(low, v.position);
                        high = glm::max(high, v.position);
                        across = std::max(across, v.uv.x);
                    }
                    if (across > 0)
                    {
                        for (park::ModelVertex& v : part.vertices)
                            v.uv.x /= across;
                    }
                    part.texture = std::string(kBoardPicture);
                    part.slots = 0;
                    const float tall = high.y - low.y;
                    if (tall > 0 && aspect == 0)
                        aspect = std::max(high.x - low.x, high.z - low.z) / tall;
                }
            }
            return aspect;
        }

        /// Uploads the pictures of `mesh`'s batches other than its board's that the scene lacks.
        void frame_textures(render::Device& device, ParkScene& scene, const park::ObjectMesh& mesh)
        {
            std::vector<park::ObjectBatch> missing;
            for (const park::ObjectBatch& batch : mesh.batches)
            {
                if (batch.picture != kBoardPicture && !scene.object_textures.contains(batch.texture))
                    missing.push_back(batch);
            }
            if (missing.empty())
                return;
            auto uploaded = ui::upload_textures(device, park::paint_batches(missing, scene.pictures));
            if (uploaded)
                scene.object_textures.merge(*uploaded);
        }

        void put_up(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            LedSigns& q = scene.led_signs;
            ui::destroy_textures(device, q.textures);
            q.signs.clear();
            std::map<std::uint64_t, std::size_t> ride_of_line;
            for (std::size_t r = 0; r < scene.rides.rides.size(); ++r)
            {
                const save::Ride& ride = scene.rides.rides[r];
                if (q.hidden.contains(ride.base.id))
                    continue;
                for (const save::Station& station : ride.stations)
                {
                    if (station.queue_line != 0)
                        ride_of_line.emplace(station.queue_line, r);
                }
            }
            // Writes `sign`'s board into its mesh and gives it a blank picture; false when `model` has no board.
            const auto hang = [&](LedSign& sign, park::ObjectModels& model) {
                const float aspect = prepare_board(model.begin()->second);
                if (aspect <= 0)
                    return false;
                sign.width = std::max<std::uint32_t>(static_cast<std::uint32_t>(std::lround(aspect * park::kSignTextHeight / park::kSignLamp)), 1) * park::kSignLamp;
                sign.board = std::format("LED sign {}", q.signs.size());
                sign.mesh = park::object_mesh(std::span(&sign.object, 1), model, scene.grid);
                for (park::ObjectBatch& batch : sign.mesh.batches)
                {
                    if (batch.picture == kBoardPicture)
                        batch.texture = sign.board;
                }
                ovl::RgbaImage blank;
                blank.width = sign.width;
                blank.height = park::kSignTextHeight;
                blank.pixels.assign(static_cast<std::size_t>(blank.width) * blank.height * 4, park::kSignDark);
                auto made = ui::upload_textures(device, { { sign.board, std::move(blank) } });
                if (!made)
                    return false;
                q.textures.merge(*made);
                return true;
            };
            // The models are read by the build beside the frame.
            wait_for_build(scene);
            for (const save::PlacedObject& o : scene.objects)
            {
                const auto found = scene.models.find(park::model_key(o));
                if (found == scene.models.end() || found->second.category != std::to_underlying(park::ItemCategory::ParkEntrance))
                    continue;
                LedSign sign;
                sign.object = o;
                park::ObjectModels model { *found };
                if (!hang(sign, model))
                    continue;
                // The entrance itself is drawn with the objects.
                std::erase_if(sign.mesh.batches, [&](const park::ObjectBatch& batch) { return batch.texture != sign.board; });
                q.signs.push_back(std::move(sign));
            }
            for (const park::QueueSignSpot& spot : park::queue_sign_spots(scene.footpaths.paths, scene.grid))
            {
                const auto ride = ride_of_line.find(spot.line);
                if (ride == ride_of_line.end())
                    continue;
                std::string theme = theme_of(scene.rides.rides[ride->second]);
                if (sign_of(q, assets, theme).empty())
                    theme = std::string(kPlainTheme);
                const std::string& symbol = sign_of(q, assets, theme);
                if (symbol.empty())
                    continue;
                LedSign sign;
                sign.ride = ride->second;
                save::PlacedObject& o = sign.object;
                o.id = std::numeric_limits<std::uint64_t>::max() - q.signs.size();
                o.owner_ref = o.id;
                o.symbol = symbol;
                o.overlay = theme;
                o.col = spot.col;
                o.row = spot.row;
                o.base_height = static_cast<std::int32_t>(std::lround(spot.height));
                o.rotation = spot.turns;
                if (!load_object_model(scene, q.models, assets, o))
                    continue;
                // A copy: the same item placed as scenery keeps its own board.
                park::ObjectModels model { { park::model_key(o), scene.models.at(park::model_key(o)) } };
                if (!hang(sign, model))
                    continue;
                frame_textures(device, scene, sign.mesh);
                q.signs.push_back(std::move(sign));
            }
        }

        const ui::Font* board_font(LedSigns& q, const ui::InterfaceAssets& assets)
        {
            if (!std::exchange(q.font_tried, true) && assets.install)
            {
                const std::string relative = ui::font_overlay(assets.fonts.language, ui::kFontSetWidths.back());
                const auto path = assets.install->find(relative);
                auto loaded = path ? ui::load_font_images(*path) : std::unexpected(relative + " not found");
                if (!loaded)
                    logging::logger("park").warning("LED signs: {}", loaded.error());
                for (ui::FontImages& f : loaded ? *loaded : std::vector<ui::FontImages> { })
                {
                    if (f.name != kBoardFont)
                        continue;
                    std::vector<ui::Font::Page> pages;
                    for (std::size_t i = 0; i < f.pages.size(); ++i)
                        pages.push_back({ static_cast<ui::TextureId>(i), static_cast<float>(f.pages[i].width), static_cast<float>(f.pages[i].height) });
                    q.font = ui::Font::from(f.font, pages, f.face);
                    q.font_pages = std::move(f.pages);
                }
            }
            return q.font ? &*q.font : nullptr;
        }
    } // namespace

    void update_led_signs(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        LedSigns& q = scene.led_signs;
        const std::array counts { scene.footpaths.paths.size(), scene.rides.rides.size(), scene.objects.size() };
        if (std::exchange(q.stale, false) || std::exchange(q.counted, counts) != counts)
            put_up(device, scene, assets);
        const ui::Font* font = board_font(q, assets);
        if (font == nullptr)
            return;
        for (LedSign& sign : q.signs)
        {
            if (sign.ride && *sign.ride >= scene.rides.rides.size())
                continue;
            const save::Park& park = scene.park_model.park;
            // An entrance shows the park's name, or that the park is closed. A ride's sign, open, shows the ride's
            // name; else what it is doing.
            std::u16string words;
            if (!sign.ride)
                words = park.flags.has(save::ParkFlag::Open) ? ui::park_name(park, *assets.strings) : std::u16string(assets.strings->get("AttractionStatusClosed"));
            else
            {
                const save::AttractionCommon& ride = scene.rides.rides[*sign.ride].base;
                words = ride.state == save::AttractionState::Open && ride.repair_status == save::RepairStatus::Fine ? ui::attraction_name(ride, *assets.strings) : ui::attraction_status(ride, *assets.strings);
            }
            if (words == sign.shown)
                continue;
            sign.shown = std::move(words);
            sign.loop = park::sign_text_loop(*font, q.font_pages, sign.shown, park::kSignTextHeight);
            sign.shift = UINT32_MAX;
        }
    }

    void roll_led_signs(render::Device& device, ParkScene& scene)
    {
        bool in_pass = true;
        for (LedSign& sign : scene.led_signs.signs)
        {
            const std::uint32_t shift = park::sign_shift(scene.clock, sign.loop.width);
            const auto texture = scene.led_signs.textures.find(sign.board);
            if (shift == sign.shift || sign.loop.width == 0 || texture == scene.led_signs.textures.end())
                continue;
            sign.shift = shift;
            if (std::exchange(in_pass, false))
                device.end_pass();
            const ovl::RgbaImage window = park::sign_window(sign.loop, shift, sign.width, sign.colour);
            device.update_texture(texture->second.handle, std::as_bytes(std::span(window.pixels)));
        }
        if (!in_pass)
        {
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }
    }

    void draw_led_signs(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        // Boards give their own light.
        render::SceneLight unlit = light;
        unlit.sun = glm::vec3(0.0f);
        unlit.sky = glm::vec3(1.0f);
        unlit.ground = glm::vec3(1.0f);
        for (const LedSign& sign : scene.led_signs.signs)
        {
            const park::ObjectMesh& mesh = sign.mesh;
            if (mesh.indices.empty())
                continue;
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
            const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
            if (!vertices || !indices)
                return;
            for (const park::ObjectBatch& batch : mesh.batches)
            {
                const bool board = batch.texture == sign.board;
                const ui::UiTextures& textures = board ? scene.led_signs.textures : scene.object_textures;
                const auto texture = textures.find(batch.texture);
                render::PipelineHandle pipeline = batch.blend == park::PartBlend::Opaque ? scene.object_pipeline : batch.blend == park::PartBlend::Cutout ? scene.cutout_pipeline : scene.see_through_pipeline;
                if (const auto front = scene.front_only.find(pipeline.id); batch.one_sided && front != scene.front_only.end())
                    pipeline = front->second;
                render::DrawCommand draw;
                draw.pipeline = pipeline;
                draw.vertices = *vertices;
                draw.indices = *indices;
                draw.index_type = render::IndexType::U32;
                draw.first = batch.first;
                draw.count = batch.count;
                draw.transform = transform;
                draw.texture = texture != textures.end() ? texture->second.handle : scene.blank;
                draw.filter = render::Filter::Linear;
                draw.address = board ? render::AddressMode::Clamp : render::AddressMode::Repeat;
                draw.light = board ? unlit : light;
                device.draw(draw);
            }
        }
    }

    void destroy_led_signs(render::Device& device, LedSigns& signs)
    {
        ui::destroy_textures(device, signs.textures);
        signs.signs.clear();
    }

} // namespace rct3::scene
