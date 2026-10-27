// SPDX-License-Identifier: GPL-3.0-or-later
//
// ovl-dump: print the structure of an OVL common/unique pair.
//
//   ovl-dump [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] [--svd] [--ban] [--bsh] [--flt] [--btbl] [--gsi] [--psi] [--tex] [--fct] [--flic] [--int] [--spl] [--san] [--modelanim] [--mdl] [--wad] [--ssk] [--mam] [--tks] [--trr] [--ric] [--rit] [--anr] [--sta] [--sat] [--tex-export DIR|-] [--obj DIR] PATH
//
// PATH names either file of the pair (X.common.ovl or X.unique.ovl) or the stem X.
#include "logging/log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

// --- scenery and placement item types (feat/ovl-scenery-types) ---
#include "ovl/carried_item.hpp"
#include "ovl/piece_set.hpp"
#include "ovl/research_category.hpp"
#include "ovl/room_type.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/terrain_type.hpp"
// --- end scenery and placement item types ---
#include "ovl/animal_model.hpp"
#include "ovl/animal_motions.hpp"
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
#include "ovl/particle_skin.hpp"
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
#include "strings/unicode.hpp"

namespace
{
    // Reports go out as plain lines, problems to standard error.
    const rct3::logging::Logger out = rct3::logging::logger("ovl-dump");
} // namespace

namespace
{

    using rct3::ovl::Part;

    struct Options
    {
        bool symbols = false;
        bool loaders = false;
        bool text = false;
        bool sound = false;
        bool ftx = false;
        bool shs = false;
        bool svd = false;
        bool ban = false;
        bool bsh = false;
        bool flic = false;
        bool int_values = false;
        bool spl = false;
        bool san = false;
        bool modelanim = false;
        bool mdl = false;
        bool wad = false;
        bool ssk = false;
        bool mam = false;
        bool fct = false;
        bool tex = false;
        bool gsi = false;
        bool btbl = false;
        bool psi = false;
        bool flt = false;

        bool tks = false;
        bool trr = false;
        bool ric = false;
        bool rit = false;
        bool anr = false;
        bool sta = false;
        bool sat = false;
    };

    void print_part(const char* title, const rct3::ovl::Overlay& overlay, const Part& part, const Options& opt)
    {
        const auto& file = part.file;
        const auto& header = file.header;
        out.info("== {}", title);

        out.info("version        {:d}", header.version);
        out.info("flags          0x{:02x}", header.flags);
        out.info("header end     0x{:x}", header.end_offset);

        out.info("external refs  {:d}", header.external_refs.size());
        for (const auto& r : header.external_refs)
            out.info("  {}", r.c_str());
        out.info("secondary refs {:d}", header.secondary_refs.size());
        for (const auto& r : header.secondary_refs)
            out.info("  {}", r.c_str());

        out.info("loaders        {:d}", header.loaders.size());
        out.info("  {:<6} {:<14} {:<4} {:<5} {}", "loader", "name", "type", "tag", "symbols");
        for (const auto& l : header.loaders)
        {
            out.info("  {:<6} {:<14} {:<4d} {:<5} {:d}", l.loader.c_str(), l.name.c_str(), l.loader_type, l.tag.c_str(), l.symbol_count);
        }

        out.info("block types");
        out.info("  {:<4} {:<7} {:<10} {:<10} {}", "type", "blocks", "bytes", "unknown", "extra");
        for (std::size_t t = 0; t < file.block_types.size(); ++t)
        {
            const auto& type = file.block_types[t];
            std::uint64_t bytes = 0;
            for (const auto& b : type.blocks)
                bytes += b.size;
            out.info("  {:<4d} {:<7d} {:<10d} {:<10d} {:d}", t, type.blocks.size(), static_cast<unsigned long long>(bytes), type.unknown, type.extra);
        }
        out.info("data size      {:d}", file.data_size);
        out.info("base           {:d}", part.base);
        out.info("unknown bytes  {:d}", file.unknown_bytes.size());
        out.info("unknown words  {:d}", file.unknown_words.size());

        // A relocation names the location of a 32-bit pointer, so all four bytes must lie in one block.
        std::size_t misplaced = 0;
        std::size_t leaving = 0; // stored pointer values past the end of the shared address space
        for (const auto address : file.relocations)
        {
            const auto at = file.locate(address, part.base);
            if (!at || at->offset + 4 > file.block_types[at->type].blocks[at->index].size)
            {
                ++misplaced;
                continue;
            }
            std::uint32_t value = 0;
            std::memcpy(&value, part.bytes.data() + file.block_types[at->type].blocks[at->index].file_offset + at->offset, 4);
            if (value >= part.base + file.data_size)
                ++leaving;
        }
        out.info("relocations    {:d} ({:d} outside blocks, {:d} pointing past own data)", file.relocations.size(), misplaced, leaving);
        std::size_t stream_pairs = 0;
        for (const auto& table : file.stream_relocations)
            stream_pairs += table.size();
        out.info("streams        {:d} ({:d} relocation pairs)", file.stream_relocations.size(), stream_pairs);

        std::size_t unresolved = 0;
        std::size_t untagged = 0;
        for (const auto& sym : part.symbols)
        {
            if (sym.name.empty())
                ++unresolved;
            else if (rct3::ovl::split_symbol_name(sym.name).tag.empty())
                ++untagged;
        }
        std::uint64_t loader_symbols = 0;
        for (const auto& l : header.loaders)
            loader_symbols += l.symbol_count;
        out.info("symbols        {:d} ({:d} unresolved names, {:d} without a tag, {:d} counted by loaders)", part.symbols.size(), unresolved, untagged, static_cast<unsigned long long>(loader_symbols));

        std::size_t chunks = 0;
        std::uint64_t chunk_bytes = 0;
        for (const auto& list : part.loaders.extra)
        {
            chunks += list.size();
            for (const auto& ch : list)
                chunk_bytes += ch.size;
        }
        std::size_t bad_type = 0;
        std::size_t unlinked = 0; // rows whose symbol address does not land on a symbol table row
        const std::size_t stride = header.version == 1 ? 12 : 16;
        for (const auto& e : part.loaders.entries)
        {
            if (e.loader_type >= header.loaders.size())
                ++bad_type;
            if (e.symbol_address == 0)
                continue;
            const auto at = file.locate(e.symbol_address, part.base);
            if (!at || at->type != 2 || at->index != 0 || at->offset % stride != 0)
                ++unlinked;
        }
        out.info("loader rows    {:d} ({:d} bad type, {:d} with an unusable symbol address)", part.loaders.entries.size(), bad_type, unlinked);
        out.info("extra chunks   {:d} ({:d} bytes)", chunks, static_cast<unsigned long long>(chunk_bytes));
        out.info("trailing       {:d} bytes after relocations", part.bytes.size() - file.end_offset);
        out.info("unparsed tail  {:d} bytes", part.bytes.size() - part.loaders.end_offset);

        if (opt.symbols)
        {
            for (const auto& sym : part.symbols)
            {
                out.info("  {:<8} {:08x} {}", sym.is_pointer ? "pointer" : "value", sym.data, sym.name.empty() ? "<unresolved>" : std::string(sym.name).c_str());
            }
        }
        if (opt.ftx)
        {
            const auto textures = rct3::ovl::read_flexi_textures(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : textures)
            {
                if (!item.texture)
                    ++bad;
            }
            out.info("flexi textures {:d} ({:d} unreadable)", textures.size(), bad);
            for (const auto& item : textures)
            {
                const std::string name(item.name);
                if (!item.texture)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& t = *item.texture;
                std::size_t alpha = 0;
                for (const auto& f : t.frames)
                    alpha += f.alpha.has_value();
                out.info("  {:<32} {:d}x{:d} frames {:d} sequence {:d} alpha {:d} animation {:d} flags {:d}", name.c_str(), t.width, t.height, t.frames.size(), t.sequence.size(), alpha, t.animation, t.flags);
            }
        }
        if (opt.shs)
        {
            const auto shapes = rct3::ovl::read_static_shapes(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : shapes)
            {
                if (!item.shape)
                    ++bad;
            }
            out.info("static shapes  {:d} ({:d} unreadable)", shapes.size(), bad);
            for (const auto& item : shapes)
            {
                const std::string name(item.name);
                if (!item.shape)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& sh = *item.shape;
                std::size_t untextured = 0;
                for (const auto& m : sh.meshes)
                    untextured += m.texture.empty();
                out.info("  {:<32} meshes {:d} vertices {:d} indices {:d} effects {:d} untextured {:d}", name.c_str(), sh.meshes.size(), sh.vertex_total, sh.index_total, sh.effects.size(), untextured);
                for (const auto& e : sh.effects)
                    out.info("    effect {} at {:.2f} {:.2f} {:.2f}", e.name, e.transform[12], e.transform[13], e.transform[14]);
            }
        }
        if (opt.svd)
        {
            const auto visuals = rct3::ovl::read_scenery_visuals(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : visuals)
            {
                if (!item.visual)
                    ++bad;
            }
            out.info("scenery visuals {:d} ({:d} unreadable)", visuals.size(), bad);
            for (const auto& item : visuals)
            {
                const std::string name(item.name);
                if (!item.visual)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& v = *item.visual;
                out.info("  {:<32} flags {:08x} sway {:g} lods {:d}", name.c_str(), v.flags, v.sway, v.lods.size());
                for (const auto& lod : v.lods)
                {
                    const std::string_view target = lod.mesh_type == 0 ? lod.static_shape : lod.mesh_type == 3 ? lod.bone_shape : lod.texture;
                    out.info("    type {:d} {:<24} distance {:g} animations {:d}", lod.mesh_type, std::string(target).c_str(), lod.distance, lod.animations.size());
                    for (const std::string_view anim : lod.animations)
                        out.info("      animation {}", anim);
                }
            }
        }
        if (opt.ban)
        {
            const auto anims = rct3::ovl::read_bone_anims(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : anims)
            {
                if (!item.anim)
                    ++bad;
            }
            out.info("bone anims     {:d} ({:d} unreadable)", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                std::size_t keys = 0;
                for (const auto& b : item.anim->bones)
                    keys += b.positions.size() + b.rotations.size();
                out.info("  {:<32} bones {:d} keyframes {:d} duration {:g}", name.c_str(), item.anim->bones.size(), keys, item.anim->duration);
            }
        }
        if (opt.bsh)
        {
            const auto shapes = rct3::ovl::read_bone_shapes(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : shapes)
            {
                if (!item.shape)
                    ++bad;
            }
            out.info("bone shapes    {:d} ({:d} unreadable)", shapes.size(), bad);
            for (const auto& item : shapes)
            {
                const std::string name(item.name);
                if (!item.shape)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& sh = *item.shape;
                std::size_t untextured = 0;
                for (const auto& m : sh.meshes)
                    untextured += m.texture.empty();
                out.info("  {:<32} meshes {:d} vertices {:d} indices {:d} bones {:d} untextured {:d}", name.c_str(), sh.meshes.size(), sh.vertex_total, sh.index_total, sh.bones.size(), untextured);
                for (const auto& b : sh.bones)
                    out.info("    bone {:<24} parent {:d} at {:g} {:g} {:g}", std::string(b.name).c_str(), b.parent, b.world[12], b.world[13], b.world[14]);
            }
        }
        if (opt.flt)
        {
            const auto floats = rct3::ovl::read_floats(overlay, part);
            out.info("float values   {:d}", floats.size());
            for (const auto& item : floats)
            {
                out.info("  {:<32} {:g}", std::string(item.name).c_str(), item.value);
            }
        }
        if (opt.int_values)
        {
            const auto values = rct3::ovl::read_integers(overlay, part);
            out.info("integer values {:d}", values.size());
            for (const auto& item : values)
            {
                out.info("  {:<32} {:d}", std::string(item.name).c_str(), item.value);
            }
        }
        if (opt.spl)
        {
            const auto splines = rct3::ovl::read_splines(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : splines)
            {
                if (!item.spline)
                    ++bad;
            }
            out.info("splines        {:d} ({:d} unreadable)", splines.size(), bad);
            for (const auto& item : splines)
            {
                const std::string name(item.name);
                if (!item.spline)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} nodes {:d} length {:g}{}", name.c_str(), item.spline->nodes.size(), item.spline->length, item.spline->closed ? " closed" : "");
                for (const auto& node : item.spline->nodes)
                    out.info("    node {:g} {:g} {:g} in {:g} {:g} {:g} out {:g} {:g} {:g}", node.position[0], node.position[1], node.position[2], node.tangent_in[0], node.tangent_in[1], node.tangent_in[2], node.tangent_out[0], node.tangent_out[1], node.tangent_out[2]);
            }
        }
        if (opt.san)
        {
            const auto anims = rct3::ovl::read_simple_anims(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : anims)
            {
                if (!item.anim)
                    ++bad;
            }
            out.info("simple anims   {:d} ({:d} unreadable)", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} keys {:d} channels {:d}", name.c_str(), item.anim->times.size(), item.anim->channels);
            }
        }
        if (opt.modelanim)
        {
            const auto anims = rct3::ovl::read_model_anims(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : anims)
            {
                if (!item.anim)
                    ++bad;
            }
            out.info("model anims    {:d} ({:d} unreadable)", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} frames {:d} duration {:g} moving {:d} turning {:d}", name.c_str(), item.anim->frame_count, item.anim->duration, item.anim->position_bones.size(), item.anim->rotation_bones.size());
            }
        }
        if (opt.mdl)
        {
            const auto models = rct3::ovl::read_animal_models(overlay, part);
            const auto bad = std::ranges::count_if(models, [](const auto& item) { return !item.model; });
            out.info("animal models  {:d} ({:d} unreadable)", models.size(), bad);
            for (const auto& item : models)
            {
                const std::string name(item.name);
                if (!item.model)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                std::string levels;
                for (const auto& level : item.model->levels)
                {
                    std::size_t vertices = 0;
                    for (const auto& mesh : level)
                        vertices += mesh.vertex_count;
                    levels += std::format(" {}/{}", level.size(), vertices);
                }
                out.info("  {:<32} bones {:d} levels (meshes/vertices){}", name.c_str(), item.model->bones.size(), levels);
                if (item.model->levels.empty())
                    continue;
                for (const auto& mesh : item.model->levels.front())
                    out.info("    material {} vertices {:d} triangles {:d}", mesh.texture, mesh.vertex_count, mesh.index_count / 3);
            }
        }
        if (opt.wad)
        {
            for (const auto& item : rct3::ovl::read_animal_motions(overlay, part))
            {
                const std::string name(item.name);
                if (!item.motions)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} slots {:d}", name.c_str(), item.motions->animations.size());
                for (std::size_t slot = 0; slot < item.motions->animations.size(); ++slot)
                {
                    if (!item.motions->animations[slot].empty())
                        out.info("    {:>2} {} {:g}s", slot, item.motions->animations[slot], item.motions->seconds[slot]);
                }
            }
        }
        if (opt.ssk)
        {
            const auto skins = rct3::ovl::read_simple_skins(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : skins)
            {
                if (!item.skin)
                    ++bad;
            }
            out.info("simple skins   {:d} ({:d} unreadable)", skins.size(), bad);
            for (const auto& item : skins)
            {
                const std::string name(item.name);
                if (!item.skin)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                std::size_t outside = 0;
                for (const auto index : item.skin->indices)
                {
                    if (index >= item.skin->vertices.size())
                        ++outside;
                }
                out.info("  {:<32} parts {:d} vertices {:d} triangles {:d} outside {:d}", name.c_str(), item.skin->part_ids.size(), item.skin->vertices.size(), item.skin->indices.size() / 3, outside);
            }
        }
        if (opt.mam)
        {
            const auto meshes = rct3::ovl::read_manifold_meshes(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : meshes)
            {
                if (!item.mesh)
                    ++bad;
            }
            out.info("manifold meshes {:d} ({:d} unreadable)", meshes.size(), bad);
            for (const auto& item : meshes)
            {
                const std::string name(item.name);
                if (!item.mesh)
                {
                    out.info("  {:<32} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} vertices {:d} triangles {:d}", name.c_str(), item.mesh->vertices.size(), item.mesh->indices.size() / 3);
            }
        }
        if (opt.tks)
        {
            const auto items = rct3::ovl::read_track_sections(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.section;
            out.info("track sections {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.section)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& s = *item.section;
                out.info(
                    "  {:<32} sid {:<24} role {:d} flags {:#x} widths {:d} {:d} side {:d} splines {} {} {} {} extra {} {} refs {:d}", name.c_str(), std::string(s.scenery_item).c_str(), s.role, s.flags[1], s.widths[0], s.widths[1], s.side, std::string(s.splines[0]).c_str(), std::string(s.splines[1]).c_str(), std::string(s.splines[2]).c_str(), std::string(s.splines[3]).c_str(), s.extra_splines[0].empty() ? "-" : std::string(s.extra_splines[0]).c_str(),
                    s.extra_splines[1].empty() ? "-" : std::string(s.extra_splines[1]).c_str(), s.references.size());
                for (const auto& r : s.references)
                    out.info("    +0x{:02x} {}:{}", r.offset, std::string(r.name).c_str(), std::string(r.tag).c_str());
            }
        }
        if (opt.ric)
        {
            const auto items = rct3::ovl::read_ride_cars(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.car;
            out.info("ride cars      {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.car)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                out.info("  {:<32} kind {:d} visual {} refs {:d}", name.c_str(), item.car->kind, std::string(item.car->visual).c_str(), item.car->references.size());
                for (const auto& ref : item.car->references)
                    out.info("    ref +{:d} {}:{}", ref.offset, std::string(ref.name).c_str(), std::string(ref.tag).c_str());
            }
        }
        if (opt.rit)
        {
            const auto items = rct3::ovl::read_ride_trains(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.train;
            out.info("ride trains    {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.train)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                std::string line = std::format("  {:<32} cars", name);
                for (const auto car : item.train->cars)
                    line += std::format(" {}", car.empty() ? std::string_view("-") : car);
                out.info("{}", line);
            }
        }
        if (opt.trr)
        {
            const auto items = rct3::ovl::read_tracked_rides(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.ride;
            out.info("tracked rides  {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.ride)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& r = *item.ride;
                out.info("  {:<32} type {:d} name {} text {} splines {} sections {}", name.c_str(), r.info.type_id, std::string(r.name).c_str(), std::string(r.info.name_text).c_str(), std::string(r.splines[0]).c_str(), std::string(r.track_sections[0]).c_str());
                out.info("    car rotation {} car facing {} most trains {}", r.rules.car_rotation, r.rules.car_facing, r.rules.most_trains ? std::format("{}", *r.rules.most_trains) : "any");
                if (const auto& d = r.operating)
                    out.info("    mode {:d} allowed 0x{:x} launch {:g} [{:g}, {:g}] lift {:g} [{:g}, {:g}] brake {:g} ({:g} N) block {:g} ({:g} N) fixed {:g} cost {:d}", d->mode, d->allowed_modes, d->launch_speed, d->launch_min, d->launch_max, d->lift_speed, d->lift_min, d->lift_max, d->brake_speed, d->brake_force, d->block_brake_speed, d->block_brake_force, d->fixed_speed.value_or(-1), d->cost);
                if (const auto& d = r.operating)
                    for (const std::string_view train : d->trains)
                        out.info("    train {}", train);
                if (const auto& d = r.operating)
                    out.info("    platform {}", d->platform);
            }
        }
        if (opt.anr)
        {
            const auto items = rct3::ovl::read_animated_rides(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.ride;
            out.info("animated rides {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.ride)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& i = item.ride->info;
                out.info("  {:<32} type {:d} text {} icon {} sid {}", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
            }
        }
        if (opt.sta)
        {
            const auto items = rct3::ovl::read_stalls(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.stall;
            out.info("stalls         {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.stall)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& i = item.stall->info;
                out.info("  {:<32} type {:d} text {} icon {} sid {}", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
            }
        }
        if (opt.sat)
        {
            const auto items = rct3::ovl::read_special_attractions(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.attraction;
            out.info("special attrs  {:d} ({:d} unreadable)", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.attraction)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& i = item.attraction->info;
                out.info("  {:<32} type {:d} text {} icon {} sid {}", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
            }
        }
        if (opt.btbl)
        {
            const auto tables = rct3::ovl::read_bitmap_tables(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : tables)
            {
                if (!item.table)
                    ++bad;
            }
            out.info("bitmap tables  {:d} ({:d} unreadable)", tables.size(), bad);
            for (const auto& item : tables)
            {
                if (!item.table)
                {
                    out.info("  row {:d} <unreadable>", item.row);
                    continue;
                }
                std::size_t bytes = 0;
                for (const auto& b : item.table->bitmaps)
                    bytes += b.data.size();
                out.info("  row {:<6d} bitmaps {:d} bytes {:d}", item.row, item.table->bitmaps.size(), bytes);
            }
        }
        if (opt.gsi)
        {
            const auto skins = rct3::ovl::read_gui_skins(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : skins)
            {
                if (!item.skin)
                    ++bad;
            }
            out.info("gui skins      {:d} ({:d} unreadable)", skins.size(), bad);
            for (const auto& item : skins)
            {
                const std::string name(item.name);
                if (!item.skin)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& s = *item.skin;
                out.info("  {:<40} kind {:d} values {:d} texture {} at {}", name.c_str(), s.kind, s.coordinates.size(), s.texture.empty() ? "<none>" : std::string(s.texture).c_str(), s.coordinates);
            }
        }
        if (opt.psi)
        {
            for (const auto& item : rct3::ovl::read_particle_skins(overlay, part))
            {
                const std::string name(item.name);
                if (!item.skin)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& s = *item.skin;
                out.info("  {:<24} frames {:<3d} {} {} loop {:.2f} texture {}", name.c_str(), s.frames.size(), s.blended ? "blended " : "additive", s.animated ? "animated" : "single  ", static_cast<double>(s.loop_time), s.texture.empty() ? "<none>" : std::string(s.texture).c_str());
            }
        }
        if (opt.tex)
        {
            const auto textures = rct3::ovl::read_textures(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : textures)
            {
                if (!item.texture)
                    ++bad;
            }
            out.info("textures       {:d} ({:d} unreadable)", textures.size(), bad);
            for (const auto& item : textures)
            {
                const std::string name(item.name);
                if (!item.texture)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& t = *item.texture;
                const std::string style(t.style);
                out.info("  {:<32} style {:<24} params {:d} {:d} {:d} first {:d} count {:d} node {}", name.c_str(), style.c_str(), t.parameters[0], t.parameters[1], t.parameters[2], t.first, t.count, t.node ? "yes" : "no");
            }
        }
        if (opt.fct)
        {
            const auto fonts = rct3::ovl::read_fonts(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : fonts)
            {
                if (!item.font)
                    ++bad;
            }
            out.info("fonts          {:d} ({:d} unreadable)", fonts.size(), bad);
            for (const auto& item : fonts)
            {
                const std::string name(item.name);
                if (!item.font)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& f = *item.font;
                out.info("  {:<28} face {:<20} texture {:<24} weight {:d} glyphs {:d}", name.c_str(), std::string(f.face).c_str(), std::string(f.texture).c_str(), f.weight, f.glyphs.size());
            }
        }
        if (opt.flic)
        {
            const auto flics = rct3::ovl::read_flics(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : flics)
            {
                if (!item.flic)
                    ++bad;
            }
            out.info("flics          {:d} ({:d} unreadable)", flics.size(), bad);
            for (const auto& item : flics)
            {
                if (!item.flic)
                {
                    out.info("  {:08x} <unreadable>", item.address);
                    continue;
                }
                const auto& f = *item.flic;
                std::string line = std::format("  {:08x} rev {:d} frames {:d} rate {:g} data {:d}", item.address, f.revision, f.frame_count, f.rate, f.data.size());
                if (f.image)
                {
                    line += std::format(" image {:d}x{:d} format {:d} levels {:d}", f.image->width, f.image->height, f.image->format, f.image->level_count);
                }
                out.info("{}", line);
            }
        }
        if (opt.sound)
        {
            const auto sounds = rct3::ovl::read_sounds(overlay, part);
            std::size_t bad = 0;
            std::size_t no_samples = 0;
            for (const auto& item : sounds)
            {
                if (!item.info)
                    ++bad;
                else if (!item.info->samples)
                    ++no_samples;
            }
            out.info("sounds         {:d} ({:d} unreadable, {:d} without samples)", sounds.size(), bad, no_samples);
            for (const auto& item : sounds)
            {
                const std::string name(item.name);
                if (!item.info)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& s = *item.info;
                out.info("  {:<32} fmt {:d} ch {:d} rate {:d} bits {:d} size {:d} addr {:08x} flag {:d}", name.c_str(), s.format_tag, s.channels, s.sample_rate, s.bits_per_sample, s.data_size, s.data_address, s.flag);
            }
        }
        if (opt.text)
        {
            const auto texts = rct3::ovl::read_texts(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : texts)
            {
                if (!item.value)
                    ++bad;
            }
            out.info("texts          {:d} ({:d} unreadable)", texts.size(), bad);
            for (const auto& item : texts)
            {
                const std::string name(item.name);
                if (item.value)
                {
                    // One line per text: line breaks and tabs inside it are written as \n, \r and \t, a backslash as \\.
                    std::string text;
                    for (const char c : rct3::strings::to_utf8(*item.value))
                        text += c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : c == '\\' ? "\\\\" : std::string(1, c);
                    out.info("  {} = {}", name.c_str(), text.c_str());
                }
                else
                    out.info("  {} = <unreadable>", name.c_str());
            }
        }
        if (opt.loaders)
        {
            for (std::size_t i = 0; i < part.loaders.entries.size(); ++i)
            {
                const auto& e = part.loaders.entries[i];
                out.info("  type {:<3d} data {:08x} symbol {:08x} resolve {:<3d} chunks {:d}", e.loader_type, e.data, e.symbol_address, e.symbols_to_resolve, part.loaders.extra[i].size());
            }
        }
    }

    // --- scenery and placement item types (feat/ovl-scenery-types) ---
    struct SceneryOptions
    {
        bool sid = false;
        bool cid = false;
        bool ced = false;
        bool rcg = false;
        bool ter = false;
        bool ptd = false;
        bool qtd = false;
        bool pty = false;
        bool enc = false;
        bool chg = false;
        bool vwg = false;
    };
    SceneryOptions g_scenery;

    // Accepts the options of this block. Returns false for any other argument.
    bool parse_scenery_option(const char* arg)
    {
        if (std::strcmp(arg, "--sid") == 0)
            g_scenery.sid = true;
        else if (std::strcmp(arg, "--cid") == 0)
            g_scenery.cid = true;
        else if (std::strcmp(arg, "--ced") == 0)
            g_scenery.ced = true;
        else if (std::strcmp(arg, "--rcg") == 0)
            g_scenery.rcg = true;
        else if (std::strcmp(arg, "--ter") == 0)
            g_scenery.ter = true;
        else if (std::strcmp(arg, "--ptd") == 0)
            g_scenery.ptd = true;
        else if (std::strcmp(arg, "--qtd") == 0)
            g_scenery.qtd = true;
        else if (std::strcmp(arg, "--pty") == 0)
            g_scenery.pty = true;
        else if (std::strcmp(arg, "--enc") == 0)
            g_scenery.enc = true;
        else if (std::strcmp(arg, "--chg") == 0)
            g_scenery.chg = true;
        else if (std::strcmp(arg, "--vwg") == 0)
            g_scenery.vwg = true;
        else
            return false;
        return true;
    }

    void print_carried_items(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        if (g_scenery.cid)
        {
            const auto items = rct3::ovl::read_carried_items(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : items)
                bad += entry.item ? 0 : 1;
            out.info("carried items ({}) {:d} ({:d} unreadable)", title, items.size(), bad);
            for (const auto& entry : items)
            {
                const std::string name(entry.name);
                if (!entry.item)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& it = *entry.item;
                const std::string text(it.name_text), remains(it.remains);
                out.info("  {:<28} flags {:08x} extras {:d} text {} remains '{}'", name.c_str(), it.flags, it.extras.size(), text.c_str(), remains.c_str());
            }
        }
        if (g_scenery.ced)
        {
            const auto extras = rct3::ovl::read_carried_item_extras(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : extras)
                bad += entry.extra ? 0 : 1;
            out.info("carried item extras ({}) {:d} ({:d} unreadable)", title, extras.size(), bad);
            for (const auto& entry : extras)
            {
                const std::string name(entry.name);
                if (!entry.extra)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.extra->name_text), icon(entry.extra->icon);
                out.info("  {:<28} text {} icon {}", name.c_str(), text.c_str(), icon.c_str());
            }
        }
    }

    void print_research_and_terrain(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        if (g_scenery.rcg)
        {
            const auto categories = rct3::ovl::read_research_categories(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : categories)
                bad += entry.category ? 0 : 1;
            out.info("research categories ({}) {:d} ({:d} unreadable)", title, categories.size(), bad);
            for (const auto& entry : categories)
            {
                const std::string name(entry.name);
                if (!entry.category)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& c = *entry.category;
                const std::string text(c.name_text);
                out.info("  {:<28} kind {:d} prerequisites {:d} text {}", name.c_str(), c.kind, c.prerequisites.size(), text.c_str());
            }
        }
        if (g_scenery.ter)
        {
            const auto types = rct3::ovl::read_terrain_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.terrain ? 0 : 1;
            out.info("terrain types ({}) {:d} ({:d} unreadable)", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.terrain)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string texture(entry.terrain->texture), label(entry.terrain->label_name);
                out.info("  {:<28} index {:d} texture {} label {}", name.c_str(), entry.terrain->index, texture.c_str(), label.c_str());
            }
        }
    }

    // One line per piece item: name text and shape of the first visual, to see that the run decoded sensibly.
    void print_piece_items(const std::vector<rct3::ovl::SceneryItem>& items)
    {
        for (const auto& it : items)
        {
            const std::string text(it.name_text), group(it.group);
            out.info("    item v{:d} type {:d} flags {:08x} squares {:d}x{:d} visuals {:d} text {} group {}", it.version, it.type, it.flags, it.x_squares, it.z_squares, it.visuals.size(), text.c_str(), group.c_str());
        }
    }

    void print_piece_sets(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        if (g_scenery.ptd)
        {
            const auto types = rct3::ovl::read_path_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.path ? 0 : 1;
            out.info("path types ({}) {:d} ({:d} unreadable)", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.path)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.path->name_text);
                out.info("  {:<20} header {:08x} names {:d} research {:d} items {:d} text {}", name.c_str(), entry.path->header_word, entry.path->piece_names.size(), entry.path->research.size(), entry.path->items.size(), text.c_str());
                std::string line = "    names";
                for (const std::string_view n : entry.path->piece_names)
                    line += std::format(" {}", n);
                out.info("{}", line);
                print_piece_items(entry.path->items);
            }
        }
        if (g_scenery.qtd)
        {
            const auto types = rct3::ovl::read_queue_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.queue ? 0 : 1;
            out.info("queue types ({}) {:d} ({:d} unreadable)", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.queue)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.queue->name_text), ground(entry.queue->ground);
                out.info("  {:<20} research {:d} items {:d} text {} ground {}", name.c_str(), entry.queue->research.size(), entry.queue->items.size(), text.c_str(), ground.c_str());
                std::string line = "    names";
                for (const std::string_view n : entry.queue->piece_names)
                    line += std::format(" {}", n);
                out.info("{}", line);
                print_piece_items(entry.queue->items);
            }
        }
        if (g_scenery.pty)
        {
            const auto types = rct3::ovl::read_pool_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.pool ? 0 : 1;
            out.info("pool types ({}) {:d} ({:d} unreadable)", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.pool)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.pool->name_text);
                out.info("  {:<20} names {:d} splines {:d} items {:d} text {}", name.c_str(), entry.pool->piece_names.size(), entry.pool->splines.size(), entry.pool->items.size(), text.c_str());
                print_piece_items(entry.pool->items);
            }
        }
        if (g_scenery.enc)
        {
            const auto types = rct3::ovl::read_enclosure_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.enclosure ? 0 : 1;
            out.info("enclosure types ({}) {:d} ({:d} unreadable)", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.enclosure)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.enclosure->name_text);
                out.info("  {:<20} items {:d} text {}", name.c_str(), entry.enclosure->items.size(), text.c_str());
                print_piece_items(entry.enclosure->items);
            }
        }
    }

    void print_rooms(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        if (g_scenery.chg)
        {
            const auto rooms = rct3::ovl::read_changing_rooms(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : rooms)
                bad += entry.room ? 0 : 1;
            out.info("changing rooms ({}) {:d} ({:d} unreadable)", title, rooms.size(), bad);
            for (const auto& entry : rooms)
            {
                const std::string name(entry.name);
                if (!entry.room)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string item(entry.room->scenery_item), text(entry.room->name_text);
                out.info("  {:<20} item {} text {}", name.c_str(), item.c_str(), text.c_str());
            }
        }
        if (g_scenery.vwg)
        {
            const auto galleries = rct3::ovl::read_viewing_galleries(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : galleries)
                bad += entry.gallery ? 0 : 1;
            out.info("viewing galleries ({}) {:d} ({:d} unreadable)", title, galleries.size(), bad);
            for (const auto& entry : galleries)
            {
                const std::string name(entry.name);
                if (!entry.gallery)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const std::string text(entry.gallery->name_text);
                out.info("  {:<20} text {}", name.c_str(), text.c_str());
            }
        }
    }

    void print_scenery_types(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        print_carried_items(title, overlay, part);
        print_research_and_terrain(title, overlay, part);
        print_piece_sets(title, overlay, part);
        print_rooms(title, overlay, part);
        if (g_scenery.sid)
        {
            const auto items = rct3::ovl::read_scenery_items(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
            {
                if (!item.item)
                    ++bad;
            }
            out.info("scenery items ({}) {:d} ({:d} unreadable)", title, items.size(), bad);
            for (const auto& entry : items)
            {
                const std::string name(entry.name);
                if (!entry.item)
                {
                    out.info("  {} <unreadable>", name.c_str());
                    continue;
                }
                const auto& it = *entry.item;
                std::size_t masks = 0, without_masks = 0;
                for (const auto& sq : it.squares)
                {
                    masks += sq.height_masks.size();
                    if (sq.height_masks.empty())
                        ++without_masks;
                }
                out.info(
                    "  {:<32} v{:d}{} type {:d} positioning {:d} flags {:08x} squares {:d}x{:d} ({:d} without masks) masks {:d} cost {:d} visuals {:d} "
                    "sounds {:d} params {:d} nodes {:d}",
                    name.c_str(), it.version, it.compact ? " compact" : "", it.type, static_cast<unsigned>(it.positioning), it.flags, it.x_squares, it.z_squares, without_masks, masks, it.cost, it.visuals.size(), it.sounds.size(), it.parameters.size(), it.nodes.size());
                for (const auto& sq : it.squares)
                    out.info("    square flags {:03x} levels {:d}..{:d}", sq.flags, sq.min_height, sq.max_height);
                for (const auto& p : it.parameters)
                    out.info("    param {} = {}", p.name, p.value);
                for (const auto& v : it.visuals)
                    out.info("    visual {}", v);
            }
        }
    }
    // --- end scenery and placement item types ---

    // --- texture decoding export (feat/texture-decode) ---
    // Decodes every flexi texture frame, bitmap-table bitmap and revision 2 flic image of a part. With a
    // directory, each image is written there as an uncompressed TGA; with "-" nothing is written and only the
    // counts are printed. Meant for checking the decoders against real files.
    const char* g_tex_export_dir = nullptr;
    std::string g_tex_export_stem;

    void export_textures(const char* title, const rct3::ovl::Overlay& overlay, const Part& part)
    {
        namespace ovl = rct3::ovl;
        const bool write = std::strcmp(g_tex_export_dir, "-") != 0;
        std::size_t frames = 0, frames_bad = 0, bitmaps = 0, bitmaps_bad = 0, flics = 0, flics_bad = 0, images = 0;
        std::size_t by_format[32] = { };

        auto save = [&](const ovl::RgbaImage& image, const std::string& name) {
            ++images;
            if (!write)
                return;
            const auto tga = ovl::encode_tga(image);
            std::string safe = name;
            for (char& c : safe)
            {
                if (c == '/' || c == '\\' || c == '\0')
                    c = '_';
            }
            const std::string path = std::format("{}/{}_{}_{}.tga", g_tex_export_dir, g_tex_export_stem, title, safe);
            if (std::FILE* f = std::fopen(path.c_str(), "wb"))
            {
                std::fwrite(tga.data(), 1, tga.size(), f);
                std::fclose(f);
            }
        };
        auto save_levels = [&](const std::vector<ovl::RgbaImage>& levels, const std::string& name) {
            for (std::size_t m = 0; m < levels.size(); ++m)
                save(levels[m], std::format("{}_m{}", name, m));
        };

        for (const auto& item : ovl::read_flexi_textures(overlay, part))
        {
            if (!item.texture)
                continue;
            for (std::size_t i = 0; i < item.texture->frames.size(); ++i)
            {
                ++frames;
                const auto image = ovl::decode_flexi_frame(item.texture->frames[i]);
                if (!image)
                {
                    ++frames_bad;
                    continue;
                }
                save(*image, std::format("ftx_{}_f{}", item.name, i));
            }
        }
        for (const auto& item : ovl::read_bitmap_tables(overlay, part))
        {
            if (!item.table)
                continue;
            for (std::size_t i = 0; i < item.table->bitmaps.size(); ++i)
            {
                const auto& b = item.table->bitmaps[i];
                ++bitmaps;
                auto levels = ovl::decode_bitmap(b);
                if (levels.empty())
                {
                    ++bitmaps_bad;
                    continue;
                }
                if (b.format < 32)
                    ++by_format[b.format];
                save_levels(levels, std::format("btbl{}_b{}", item.row, i));
            }
        }
        for (const auto& item : ovl::read_flics(overlay, part))
        {
            if (!item.flic || item.flic->revision != 2)
                continue;
            ++flics;
            auto levels = ovl::decode_flic_image(*item.flic);
            if (levels.empty())
            {
                ++flics_bad;
                continue;
            }
            if (item.flic->image && item.flic->image->format < 32)
                ++by_format[item.flic->image->format];
            save_levels(levels, std::format("flic{}", item.address));
        }
        out.info("tex export     {}: frames {:d} ({:d} failed) bitmaps {:d} ({:d} failed) flics {:d} ({:d} failed) images {:d}", title, frames, frames_bad, bitmaps, bitmaps_bad, flics, flics_bad, images);
        for (std::size_t f = 0; f < 32; ++f)
        {
            if (by_format[f])
                out.info("tex format     {}: {:d} x format {:d}", title, by_format[f], f);
        }
    }
    // --- end texture decoding export ---

    // --- mesh export (feat/mesh-semantics) ---
    // Writes every static shape and bone shape (rest pose) of one part as <prefix>__<name>.obj and .mtl in
    // `dir`. Returns false when a file cannot be written.
    std::string export_file_name(std::string_view prefix, std::string_view name, const char* tag)
    {
        std::string out(prefix);
        out += "__";
        out += name;
        for (char& c : out)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!ok)
                c = '_';
        }
        return std::format("{}.{}", out, tag);
    }

    bool write_text_file(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(f);
    }

    bool export_mesh(const std::filesystem::path& dir, std::string_view prefix, std::string_view name, const char* kind, const rct3::ovl::Mesh& mesh)
    {
        const std::string stem = export_file_name(prefix, std::format("{}_{}", kind, name), "obj");
        const std::string base = stem.substr(0, stem.size() - 4);
        return write_text_file(dir / std::format("{}.obj", base), rct3::ovl::write_obj(mesh, std::format("{}.mtl", base))) && write_text_file(dir / std::format("{}.mtl", base), rct3::ovl::write_mtl(mesh));
    }

    bool export_part_meshes(const std::filesystem::path& dir, std::string_view prefix, const rct3::ovl::Overlay& overlay, const Part& part, std::size_t& written, std::size_t& failed)
    {
        bool ok = true;
        for (const auto& item : rct3::ovl::read_static_shapes(overlay, part))
        {
            if (!item.shape)
            {
                ++failed;
                out.error("  {} <unreadable static shape>", std::string(item.name).c_str());
                continue;
            }
            if (export_mesh(dir, prefix, item.name, "shs", rct3::ovl::mesh_from_static_shape(*item.shape)))
                ++written;
            else
                ok = false, ++failed;
        }
        for (const auto& item : rct3::ovl::read_bone_shapes(overlay, part))
        {
            if (!item.shape)
            {
                ++failed;
                out.error("  {} <unreadable bone shape>", std::string(item.name).c_str());
                continue;
            }
            if (export_mesh(dir, prefix, item.name, "bsh", rct3::ovl::mesh_from_bone_shape(*item.shape)))
                ++written;
            else
                ok = false, ++failed;
        }
        return ok;
    }
    // --- end mesh export ---

} // namespace

int main(int argc, char** argv)
{
    rct3::logging::configure({ .memory_capacity = 0, .plain = true });
    Options opt;
    const char* path = nullptr;
    const char* obj_dir = nullptr; // mesh export directory
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--symbols") == 0)
            opt.symbols = true;
        else if (std::strcmp(argv[i], "--loaders") == 0)
            opt.loaders = true;
        else if (std::strcmp(argv[i], "--text") == 0)
            opt.text = true;
        else if (std::strcmp(argv[i], "--sound") == 0)
            opt.sound = true;
        else if (std::strcmp(argv[i], "--ftx") == 0)
            opt.ftx = true;
        else if (std::strcmp(argv[i], "--shs") == 0)
            opt.shs = true;
        else if (std::strcmp(argv[i], "--svd") == 0)
            opt.svd = true;
        else if (std::strcmp(argv[i], "--ban") == 0)
            opt.ban = true;
        else if (std::strcmp(argv[i], "--bsh") == 0)
            opt.bsh = true;
        else if (std::strcmp(argv[i], "--flic") == 0)
            opt.flic = true;
        else if (std::strcmp(argv[i], "--int") == 0)
            opt.int_values = true;
        else if (std::strcmp(argv[i], "--spl") == 0)
            opt.spl = true;
        else if (std::strcmp(argv[i], "--san") == 0)
            opt.san = true;
        else if (std::strcmp(argv[i], "--modelanim") == 0)
            opt.modelanim = true;
        else if (std::strcmp(argv[i], "--mdl") == 0)
            opt.mdl = true;
        else if (std::strcmp(argv[i], "--wad") == 0)
            opt.wad = true;
        else if (std::strcmp(argv[i], "--ssk") == 0)
            opt.ssk = true;
        else if (std::strcmp(argv[i], "--mam") == 0)
            opt.mam = true;
        else if (std::strcmp(argv[i], "--fct") == 0)
            opt.fct = true;
        else if (std::strcmp(argv[i], "--tex") == 0)
            opt.tex = true;
        else if (std::strcmp(argv[i], "--gsi") == 0)
            opt.gsi = true;
        else if (std::strcmp(argv[i], "--btbl") == 0)
            opt.btbl = true;
        else if (std::strcmp(argv[i], "--psi") == 0)
            opt.psi = true;
        else if (std::strcmp(argv[i], "--flt") == 0)
            opt.flt = true;
        else if (std::strcmp(argv[i], "--tex-export") == 0 && i + 1 < argc)
            g_tex_export_dir = argv[++i];
        else if (parse_scenery_option(argv[i]))
        {
        }
        else if (std::strcmp(argv[i], "--tks") == 0)
            opt.tks = true;
        else if (std::strcmp(argv[i], "--trr") == 0)
            opt.trr = true;
        else if (std::strcmp(argv[i], "--ric") == 0)
            opt.ric = true;
        else if (std::strcmp(argv[i], "--rit") == 0)
            opt.rit = true;
        else if (std::strcmp(argv[i], "--anr") == 0)
            opt.anr = true;
        else if (std::strcmp(argv[i], "--sta") == 0)
            opt.sta = true;
        else if (std::strcmp(argv[i], "--sat") == 0)
            opt.sat = true;
        // --- mesh export option ---
        else if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc)
            obj_dir = argv[++i];
        // --- end mesh export option ---
        else if (!path)
            path = argv[i];
        else
            path = nullptr, i = argc; // more than one path: usage error below
    }
    if (!path)
    {
        out.error("usage: {} [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] [--svd] [--ban] [--bsh] [--flt] [--btbl] [--gsi] [--psi] [--tex] [--fct] [--flic] [--int] [--spl] [--san] [--modelanim] [--mdl] [--wad] [--ssk] [--mam] [--tks] [--trr] [--ric] [--rit] [--anr] [--sta] [--sat] [--tex-export DIR|-] [--obj DIR] PATH", argv[0]);
        out.error("scenery types: [--sid] [--cid] [--ced] [--rcg] [--ter] [--ptd] [--qtd] [--pty] [--enc] [--chg] [--vwg]");
        return 2;
    }

    auto overlay = rct3::ovl::Overlay::load(path);
    if (!overlay)
    {
        out.error("{}: {}", path, rct3::ovl::to_string(overlay.error()));
        return 1;
    }

    if (g_tex_export_dir)
    {
        g_tex_export_stem = std::filesystem::path(path).stem().string();
        if (std::strcmp(g_tex_export_dir, "-") != 0)
            std::filesystem::create_directories(g_tex_export_dir);
    }

    print_part("common", *overlay, overlay->common(), opt);
    if (const auto unique = overlay->unique())
        print_part("unique", *overlay, *unique, opt);
    else
        out.info("== unique\n(none)");
    if (g_tex_export_dir)
    {
        export_textures("common", *overlay, overlay->common());
        if (const auto unique = overlay->unique())
            export_textures("unique", *overlay, *unique);
    }
    print_scenery_types("common", *overlay, overlay->common());
    if (const auto unique = overlay->unique())
        print_scenery_types("unique", *overlay, *unique);
    // --- mesh export ---
    if (obj_dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(obj_dir, ec);
        if (ec)
        {
            out.error("{}: {}", obj_dir, ec.message().c_str());
            return 1;
        }
        const std::string prefix = std::filesystem::path(path).filename().string();
        std::string stem = prefix;
        for (const char* suffix : { ".common.ovl", ".unique.ovl", ".ovl" })
        {
            const std::string_view s = suffix;
            if (stem.size() > s.size() && stem.compare(stem.size() - s.size(), s.size(), s) == 0)
            {
                stem.resize(stem.size() - s.size());
                break;
            }
        }
        std::size_t written = 0, failed = 0;
        bool ok = export_part_meshes(obj_dir, std::format("{}_c", stem), *overlay, overlay->common(), written, failed);
        if (const auto unique = overlay->unique())
            ok = export_part_meshes(obj_dir, std::format("{}_u", stem), *overlay, *unique, written, failed) && ok;
        out.info("exported meshes {:d} ({:d} failed)", written, failed);
        if (!ok || failed != 0)
            return 1;
    }
    // --- end mesh export ---
    return 0;
}
