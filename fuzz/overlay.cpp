// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: a lone common file through the overlay loader and every item reader.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// --- scenery and placement item types (feat/ovl-scenery-types) ---
#include "ovl/carried_item.hpp"
#include "ovl/piece_set.hpp"
#include "ovl/research_category.hpp"
#include "ovl/room_type.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/terrain_type.hpp"
// --- end scenery and placement item types ---
#include "ovl/animated_ride.hpp"
#include "ovl/bitmap_table.hpp"
#include "ovl/bone_anim.hpp"
#include "ovl/bone_shape.hpp"
#include "ovl/flexi_texture.hpp"
#include "ovl/flic.hpp"
#include "ovl/float_value.hpp"
#include "ovl/font.hpp"
#include "ovl/gui_skin.hpp"
#include "ovl/integer_value.hpp"
#include "ovl/manifold_mesh.hpp"
#include "ovl/mesh.hpp"
#include "ovl/model_anim.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ride_car.hpp"
#include "ovl/ride_train.hpp"
#include "ovl/scenery_visual.hpp"
#include "ovl/simple_anim.hpp"
#include "ovl/simple_skin.hpp"
#include "ovl/sound.hpp"
#include "ovl/special_attraction.hpp"
#include "ovl/spline.hpp"
#include "ovl/stall.hpp"
#include "ovl/static_shape.hpp"
#include "ovl/text.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_image.hpp"
#include "ovl/track_section.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/build_catalogue.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data) + size);
    const auto overlay = rct3::ovl::Overlay::from_bytes(std::move(bytes), std::nullopt);
    if (!overlay)
        return 0;

    const auto& part = overlay->common();
    volatile std::size_t sink = 0;
    sink += rct3::ovl::read_texts(*overlay, part).size();
    sink += rct3::ovl::read_sounds(*overlay, part).size();
    sink += rct3::ovl::read_flexi_textures(*overlay, part).size();
    // --- texture decoding (feat/texture-decode) ---
    for (const auto& item : rct3::ovl::read_flexi_textures(*overlay, part))
    {
        if (!item.texture)
            continue;
        for (const auto& frame : item.texture->frames)
        {
            if (const auto image = rct3::ovl::decode_flexi_frame(frame))
                sink += image->pixels.size();
        }
    }
    for (const auto& item : rct3::ovl::read_bitmap_tables(*overlay, part))
    {
        if (!item.table)
            continue;
        for (const auto& bitmap : item.table->bitmaps)
        {
            for (const auto& level : rct3::ovl::decode_bitmap(bitmap))
                sink += level.pixels.size();
        }
    }
    for (const auto& item : rct3::ovl::read_flics(*overlay, part))
    {
        if (!item.flic)
            continue;
        for (const auto& level : rct3::ovl::decode_flic_image(*item.flic))
            sink += level.pixels.size();
    }
    // --- end texture decoding ---
    for (const auto& item : rct3::ovl::read_static_shapes(*overlay, part))
    {
        if (!item.shape)
            continue;
        for (const auto& mesh : item.shape->meshes)
        {
            for (std::size_t i = 0; i < mesh.vertex_count; ++i)
                sink += mesh.vertex(i).colour[3];
            for (std::size_t i = 0; i < mesh.index_count; ++i)
                sink += mesh.index(i);
        }
    }
    for (const auto& item : rct3::ovl::read_scenery_visuals(*overlay, part))
    {
        if (item.visual)
            sink += item.visual->lods.size();
    }
    for (const auto& item : rct3::ovl::read_bone_anims(*overlay, part))
    {
        if (!item.anim)
            continue;
        for (const auto& bone : item.anim->bones)
            sink += bone.positions.size() + bone.rotations.size();
    }
    for (const auto& item : rct3::ovl::read_bone_shapes(*overlay, part))
    {
        if (!item.shape)
            continue;
        for (const auto& mesh : item.shape->meshes)
        {
            for (std::size_t i = 0; i < mesh.vertex_count; ++i)
                sink += mesh.vertex(i).weights[0];
            for (std::size_t i = 0; i < mesh.index_count; ++i)
                sink += mesh.index(i);
        }
    }
    sink += rct3::ovl::read_floats(*overlay, part).size();
    sink += rct3::ovl::read_integers(*overlay, part).size();
    for (const auto& item : rct3::ovl::read_manifold_meshes(*overlay, part))
    {
        if (!item.mesh)
            continue;
        sink += item.mesh->vertices.size();
        for (const auto index : item.mesh->indices)
            sink += index;
    }
    for (const auto& item : rct3::ovl::read_model_anims(*overlay, part))
    {
        if (item.anim)
            sink += item.anim->positions.size() + item.anim->rotations.size() + item.anim->position_bones.size();
    }
    for (const auto& item : rct3::ovl::read_simple_skins(*overlay, part))
    {
        if (!item.skin)
            continue;
        sink += item.skin->vertices.size();
        for (const auto index : item.skin->indices)
            sink += index;
    }
    for (const auto& item : rct3::ovl::read_simple_anims(*overlay, part))
    {
        if (item.anim)
            sink += item.anim->times.size() + item.anim->samples.size();
    }
    for (const auto& item : rct3::ovl::read_splines(*overlay, part))
    {
        if (item.spline)
            sink += item.spline->nodes.size() + item.spline->segment_lengths.size();
    }
    for (const auto& item : rct3::ovl::read_track_sections(*overlay, part))
    {
        if (item.section)
            sink += item.section->references.size() + item.section->record.size();
    }
    for (const auto& item : rct3::ovl::read_ride_cars(*overlay, part))
    {
        if (item.car)
            sink += item.car->references.size() + item.car->record.size();
    }
    for (const auto& item : rct3::ovl::read_ride_trains(*overlay, part))
    {
        if (item.train)
            sink += item.train->references.size() + item.train->record.size();
    }
    for (const auto& item : rct3::ovl::read_tracked_rides(*overlay, part))
        sink += item.ride.has_value();
    for (const auto& item : rct3::ovl::read_animated_rides(*overlay, part))
        sink += item.ride.has_value();
    for (const auto& item : rct3::ovl::read_stalls(*overlay, part))
        sink += item.stall.has_value();
    for (const auto& item : rct3::ovl::read_special_attractions(*overlay, part))
        sink += item.attraction.has_value();
    for (const auto& item : rct3::ovl::read_bitmap_tables(*overlay, part))
    {
        if (!item.table)
            continue;
        for (const auto& bitmap : item.table->bitmaps)
            sink += bitmap.data.size() + bitmap.levels;
    }
    for (const auto& item : rct3::ovl::read_gui_skins(*overlay, part))
    {
        if (item.skin)
            sink += item.skin->coordinates.size();
    }
    for (const auto& item : rct3::ovl::read_textures(*overlay, part))
    {
        if (item.texture)
            sink += item.texture->count + (item.texture->node ? item.texture->node->second : 0);
    }
    for (const auto& item : rct3::ovl::read_fonts(*overlay, part))
    {
        if (!item.font)
            continue;
        sink += item.font->face.size() + item.font->glyphs.size();
        for (const auto& g : item.font->glyphs)
            sink += g.code;
    }
    for (const auto& item : rct3::ovl::read_flics(*overlay, part))
    {
        if (!item.flic)
            continue;
        sink += item.flic->frames.size() + item.flic->data.size();
    }
    // --- scenery and placement item types (feat/ovl-scenery-types) ---
    for (const auto& entry : rct3::ovl::read_scenery_items(*overlay, part))
    {
        if (!entry.item)
            continue;
        const auto& it = *entry.item;
        sink += it.squares.size() + it.nodes.size() + it.visuals.size() + it.sounds.size() + it.parameters.size();
        for (const auto& sq : it.squares)
            sink += sq.height_masks.size() + sq.flags;
        for (const auto& sound : it.sounds)
            sink += sound.sounds.size() + sound.script_count;
        sink += it.type + it.flags;
    }
    for (const auto& entry : rct3::ovl::read_carried_items(*overlay, part))
    {
        if (!entry.item)
            continue;
        sink += entry.item->extras.size() + entry.item->flags + entry.item->amounts[0];
    }
    for (const auto& entry : rct3::ovl::read_carried_item_extras(*overlay, part))
    {
        if (entry.extra)
            sink += entry.extra->unknown_0c;
    }
    for (const auto& entry : rct3::ovl::read_research_categories(*overlay, part))
    {
        if (entry.category)
            sink += entry.category->prerequisites.size() + entry.category->kind;
    }
    for (const auto& entry : rct3::ovl::read_terrain_types(*overlay, part))
    {
        if (entry.terrain)
            sink += entry.terrain->index + entry.terrain->colours[0];
    }
    for (const auto& entry : rct3::ovl::read_path_types(*overlay, part))
    {
        if (!entry.path)
            continue;
        sink += entry.path->items.size() + entry.path->piece_names.size() + entry.path->research.size();
        for (const auto& it : entry.path->items)
            sink += it.squares.size() + it.visuals.size();
    }
    for (const auto& entry : rct3::ovl::read_queue_types(*overlay, part))
    {
        if (!entry.queue)
            continue;
        sink += entry.queue->items.size() + entry.queue->research.size();
        for (const auto& it : entry.queue->items)
            sink += it.squares.size() + it.visuals.size();
    }
    for (const auto& entry : rct3::ovl::read_pool_types(*overlay, part))
    {
        if (!entry.pool)
            continue;
        sink += entry.pool->items.size() + entry.pool->splines.size() + static_cast<std::size_t>(entry.pool->unknown_values[3] > 0);
    }
    for (const auto& entry : rct3::ovl::read_enclosure_types(*overlay, part))
    {
        if (entry.enclosure)
            sink += entry.enclosure->items.size() + entry.enclosure->piece_names.size();
    }
    for (const auto& entry : rct3::ovl::read_changing_rooms(*overlay, part))
    {
        if (entry.room)
            sink += entry.room->unknown_0c + entry.room->unknown_2c;
    }
    for (const auto& entry : rct3::ovl::read_viewing_galleries(*overlay, part))
    {
        if (entry.gallery)
            sink += entry.gallery->unknown_04 + entry.gallery->unknown_24;
    }
    // --- end scenery and placement item types ---
    // --- mesh conversion (feat/mesh-semantics) ---
    // Convert bounded numbers of shapes; sinks are sizes and indices only, never floats cast to integers.
    {
        std::size_t budget = 64;
        for (const auto& item : rct3::ovl::read_static_shapes(*overlay, part))
        {
            if (!item.shape || budget == 0)
                continue;
            --budget;
            const auto mesh = rct3::ovl::mesh_from_static_shape(*item.shape);
            sink += mesh.positions.size() + mesh.parts.size();
            for (const auto index : mesh.indices)
            {
                if (index >= mesh.positions.size())
                    __builtin_trap();
            }
            if (budget % 8 == 0)
                sink += rct3::ovl::write_obj(mesh, "m.mtl").size() + rct3::ovl::write_mtl(mesh).size();
        }
        for (const auto& item : rct3::ovl::read_bone_shapes(*overlay, part))
        {
            if (!item.shape || budget == 0)
                continue;
            --budget;
            const auto mesh = rct3::ovl::mesh_from_bone_shape(*item.shape);
            sink += mesh.positions.size() + mesh.parts.size();
            for (const auto index : mesh.indices)
            {
                if (index >= mesh.positions.size())
                    __builtin_trap();
            }
        }
    }
    // --- end mesh conversion ---
    {
        // What the build lists offer: rides, shops and scenery with the scenery items they are placed as.
        std::vector<rct3::park::BuildItem> items;
        rct3::park::add_build_items(*overlay, "Fuzz/Style", items);
        sink += items.size();
    }
    return 0;
}
