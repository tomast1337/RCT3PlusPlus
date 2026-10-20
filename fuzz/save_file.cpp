// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: read_save must return a value or an error for any input, never crash, read out of
// bounds or trip a sanitizer. Run with `./nob fuzz save_file`.
#include "save/save_file.hpp"

#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/tutorial.hpp"
#include "save/model/world.hpp"
#include "save/value.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    // ---- world model fuzz block begin ----
    // The blob decoders take any bytes: run them on the raw input, and on the same input shifted by one.
    for (const auto view : { bytes, bytes.empty() ? bytes : bytes.subspan(1) })
    {
        if (const auto g = rct3::save::decode_terrain(view))
        {
            if (g->cells.size() != static_cast<std::size_t>(g->width) * static_cast<std::size_t>(g->height))
                __builtin_trap();
            if (g->cells.size() > rct3::save::kMaxTerrainCells)
                __builtin_trap();
            (void)g->height_at(0.0f, 0.0f);
            (void)g->height_bilinear_at(g->origin_x + 1.0f, g->origin_z + 1.0f);
        }
        if (const auto w = rct3::save::decode_water(view))
        {
            if (w->triangle_count() * 4 + w->bodies.size() * 8 + 6 != view.size())
                __builtin_trap();
        }
        if (const auto t = rct3::save::decode_skirt_trees(view))
        {
            if (t->size() * 20 + 4 != view.size())
                __builtin_trap();
        }
    }
    // ---- world model fuzz block end ----

    const auto file = rct3::save::read_save(bytes, { .verify_checksum = false });
    if (!file)
        return 0;

    // Sections tile the input exactly, in order.
    std::size_t at = 0;
    for (const auto& s : file->sections)
    {
        if (s.offset != at)
            __builtin_trap();
        at += s.size;
    }
    if (at != size)
        __builtin_trap();

    // Every object lies inside the input, starts with a valid class and is at least the fixed prefix.
    const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
    for (const auto& o : file->objects)
    {
        if (o.class_index >= file->classes.size())
            __builtin_trap();
        if (o.bytes.size() < 12 || o.offset > size || o.bytes.size() > size - o.offset)
            __builtin_trap();
        if (o.bytes.data() != begin + o.offset)
            __builtin_trap();
    }
    // Names are views into the input.
    for (const auto& c : file->classes)
    {
        if (c.name.empty())
            continue;
        const auto* p = reinterpret_cast<const std::byte*>(c.name.data());
        if (p < begin || p + c.name.size() > begin + size)
            __builtin_trap();
    }

    // The value decoder must accept every object the container reader accepted, and never produce more values
    // than there are payload bytes.
    const rct3::save::ObjectIndex index(*file);
    for (const auto& o : file->objects)
    {
        const auto decoded = rct3::save::decode_object(*file, o);
        if (!decoded)
            __builtin_trap();
        if (decoded->stats.values > 2 * o.bytes.size() + file->classes[o.class_index].attributes.size())
            __builtin_trap();
        if (decoded->fields.items().size() != file->classes[o.class_index].attributes.size())
            __builtin_trap();
        if (!index.find(o.id))
            __builtin_trap();
    }
    // BEGIN park: the park loader accepts everything that decodes and yields a self-consistent model.
    {
        const auto model = rct3::save::load_park(*file, index);
        if (!model)
            __builtin_trap();
        if (model->messages.ordered.size() > model->messages.ring_size)
            __builtin_trap();
        if (model->content.entries.size() + model->content.entries_without_overlay > file->objects.size())
        {
            __builtin_trap();
        }
        for (const auto& e : model->content.entries)
        {
            if (e.overlay_stem.empty())
                __builtin_trap();
        }
        (void)rct3::save::validate(*model);
    }
    // END park

    // --- people model loader (begin) ---
    // Must accept anything the decoder accepted, and its validation must terminate without trapping.
    {
        const auto people = rct3::save::load_people(*file, index);
        if (!people)
            __builtin_trap();
        (void)rct3::save::validate(*people);
    }
    // --- people model loader (end) ---

    // ---- world model fuzz block begin ----
    // The loader never fails on a file whose objects all decode; its parts must stay consistent.
    const auto world = rct3::save::load_world(*file, index);
    if (!world)
        __builtin_trap();
    if (world->terrain.cells.size() != static_cast<std::size_t>(world->terrain.width) * static_cast<std::size_t>(world->terrain.height))
        __builtin_trap();
    if (world->objects.size() + world->paths.size() > file->objects.size())
        __builtin_trap();
    // ---- world model fuzz block end ----
    // The tutorial loader takes whatever decodes.
    if (!rct3::save::load_tutorial(*file))
        __builtin_trap();
    // --- rides: begin ---
    // The ride loader must accept anything that decodes, with every walk bounded by the input.
    {
        const auto model = rct3::save::load_rides(*file, index);
        if (!model)
            __builtin_trap();
        std::size_t pieces = 0;
        for (const auto& r : model->rides)
        {
            if (r.track)
                pieces += r.track->pieces.size();
        }
        if (pieces > file->objects.size())
            __builtin_trap();
    }
    // --- rides: end ---
    return 0;
}
