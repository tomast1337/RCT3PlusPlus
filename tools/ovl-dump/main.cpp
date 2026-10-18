// SPDX-License-Identifier: GPL-3.0-or-later
//
// ovl-dump: print the structure of an OVL common/unique pair.
//
//   ovl-dump [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] [--svd] [--ban] [--bsh] [--flt] [--btbl] [--gsi] [--tex] [--fct] [--flic] [--int] [--spl] [--san] [--modelanim] [--ssk] [--mam] [--tks] [--trr] [--ric] [--rit] [--anr] [--sta] [--sat] [--tex-export DIR|-] [--obj DIR] PATH
//
// PATH names either file of the pair (X.common.ovl or X.unique.ovl) or the stem X.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
#include "strings/unicode.hpp"

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
        bool ssk = false;
        bool mam = false;
        bool fct = false;
        bool tex = false;
        bool gsi = false;
        bool btbl = false;
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
        std::printf("== %s\n", title);

        std::printf("version        %u\n", header.version);
        std::printf("flags          0x%02x\n", header.flags);
        std::printf("header end     0x%zx\n", header.end_offset);

        std::printf("external refs  %zu\n", header.external_refs.size());
        for (const auto& r : header.external_refs)
            std::printf("  %s\n", r.c_str());
        std::printf("secondary refs %zu\n", header.secondary_refs.size());
        for (const auto& r : header.secondary_refs)
            std::printf("  %s\n", r.c_str());

        std::printf("loaders        %zu\n", header.loaders.size());
        std::printf("  %-6s %-14s %-4s %-5s %s\n", "loader", "name", "type", "tag", "symbols");
        for (const auto& l : header.loaders)
        {
            std::printf("  %-6s %-14s %-4u %-5s %u\n", l.loader.c_str(), l.name.c_str(), l.loader_type, l.tag.c_str(), l.symbol_count);
        }

        std::printf("block types\n");
        std::printf("  %-4s %-7s %-10s %-10s %s\n", "type", "blocks", "bytes", "unknown", "extra");
        for (std::size_t t = 0; t < file.block_types.size(); ++t)
        {
            const auto& type = file.block_types[t];
            std::uint64_t bytes = 0;
            for (const auto& b : type.blocks)
                bytes += b.size;
            std::printf("  %-4zu %-7zu %-10llu %-10u %u\n", t, type.blocks.size(), static_cast<unsigned long long>(bytes), type.unknown, type.extra);
        }
        std::printf("data size      %u\n", file.data_size);
        std::printf("base           %u\n", part.base);
        std::printf("unknown bytes  %zu\n", file.unknown_bytes.size());
        std::printf("unknown words  %zu\n", file.unknown_words.size());

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
        std::printf("relocations    %zu (%zu outside blocks, %zu pointing past own data)\n", file.relocations.size(), misplaced, leaving);
        std::size_t stream_pairs = 0;
        for (const auto& table : file.stream_relocations)
            stream_pairs += table.size();
        std::printf("streams        %zu (%zu relocation pairs)\n", file.stream_relocations.size(), stream_pairs);

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
        std::printf("symbols        %zu (%zu unresolved names, %zu without a tag, %llu counted by loaders)\n", part.symbols.size(), unresolved, untagged, static_cast<unsigned long long>(loader_symbols));

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
        std::printf("loader rows    %zu (%zu bad type, %zu with an unusable symbol address)\n", part.loaders.entries.size(), bad_type, unlinked);
        std::printf("extra chunks   %zu (%llu bytes)\n", chunks, static_cast<unsigned long long>(chunk_bytes));
        std::printf("trailing       %zu bytes after relocations\n", part.bytes.size() - file.end_offset);
        std::printf("unparsed tail  %zu bytes\n", part.bytes.size() - part.loaders.end_offset);

        if (opt.symbols)
        {
            for (const auto& sym : part.symbols)
            {
                std::printf("  %-8s %08x %s\n", sym.is_pointer ? "pointer" : "value", sym.data, sym.name.empty() ? "<unresolved>" : std::string(sym.name).c_str());
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
            std::printf("flexi textures %zu (%zu unreadable)\n", textures.size(), bad);
            for (const auto& item : textures)
            {
                const std::string name(item.name);
                if (!item.texture)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& t = *item.texture;
                std::size_t alpha = 0;
                for (const auto& f : t.frames)
                    alpha += f.alpha.has_value();
                std::printf("  %-32s %ux%u frames %zu sequence %zu alpha %zu animation %u flags %u\n", name.c_str(), t.width, t.height, t.frames.size(), t.sequence.size(), alpha, t.animation, t.flags);
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
            std::printf("static shapes  %zu (%zu unreadable)\n", shapes.size(), bad);
            for (const auto& item : shapes)
            {
                const std::string name(item.name);
                if (!item.shape)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& sh = *item.shape;
                std::size_t untextured = 0;
                for (const auto& m : sh.meshes)
                    untextured += m.texture.empty();
                std::printf("  %-32s meshes %zu vertices %u indices %u effects %zu untextured %zu\n", name.c_str(), sh.meshes.size(), sh.vertex_total, sh.index_total, sh.effects.size(), untextured);
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
            std::printf("scenery visuals %zu (%zu unreadable)\n", visuals.size(), bad);
            for (const auto& item : visuals)
            {
                const std::string name(item.name);
                if (!item.visual)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& v = *item.visual;
                std::printf("  %-32s flags %08x sway %g lods %zu\n", name.c_str(), v.flags, v.sway, v.lods.size());
                for (const auto& lod : v.lods)
                {
                    const std::string_view target = lod.mesh_type == 0 ? lod.static_shape : lod.mesh_type == 3 ? lod.bone_shape : lod.texture;
                    std::printf("    type %u %-24s distance %g animations %zu\n", lod.mesh_type, std::string(target).c_str(), lod.distance, lod.animations.size());
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
            std::printf("bone anims     %zu (%zu unreadable)\n", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                std::size_t keys = 0;
                for (const auto& b : item.anim->bones)
                    keys += b.positions.size() + b.rotations.size();
                std::printf("  %-32s bones %zu keyframes %zu duration %g\n", name.c_str(), item.anim->bones.size(), keys, item.anim->duration);
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
            std::printf("bone shapes    %zu (%zu unreadable)\n", shapes.size(), bad);
            for (const auto& item : shapes)
            {
                const std::string name(item.name);
                if (!item.shape)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& sh = *item.shape;
                std::size_t untextured = 0;
                for (const auto& m : sh.meshes)
                    untextured += m.texture.empty();
                std::printf("  %-32s meshes %zu vertices %u indices %u bones %zu untextured %zu\n", name.c_str(), sh.meshes.size(), sh.vertex_total, sh.index_total, sh.bones.size(), untextured);
            }
        }
        if (opt.flt)
        {
            const auto floats = rct3::ovl::read_floats(overlay, part);
            std::printf("float values   %zu\n", floats.size());
            for (const auto& item : floats)
            {
                std::printf("  %-32s %g\n", std::string(item.name).c_str(), item.value);
            }
        }
        if (opt.int_values)
        {
            const auto values = rct3::ovl::read_integers(overlay, part);
            std::printf("integer values %zu\n", values.size());
            for (const auto& item : values)
            {
                std::printf("  %-32s %d\n", std::string(item.name).c_str(), item.value);
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
            std::printf("splines        %zu (%zu unreadable)\n", splines.size(), bad);
            for (const auto& item : splines)
            {
                const std::string name(item.name);
                if (!item.spline)
                {
                    std::printf("  %-32s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s nodes %zu length %g%s\n", name.c_str(), item.spline->nodes.size(), item.spline->length, item.spline->closed ? " closed" : "");
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
            std::printf("simple anims   %zu (%zu unreadable)\n", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    std::printf("  %-32s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s keys %zu channels %u\n", name.c_str(), item.anim->times.size(), item.anim->channels);
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
            std::printf("model anims    %zu (%zu unreadable)\n", anims.size(), bad);
            for (const auto& item : anims)
            {
                const std::string name(item.name);
                if (!item.anim)
                {
                    std::printf("  %-32s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s frames %u duration %g moving %zu turning %zu\n", name.c_str(), item.anim->frame_count, item.anim->duration, item.anim->position_bones.size(), item.anim->rotation_bones.size());
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
            std::printf("simple skins   %zu (%zu unreadable)\n", skins.size(), bad);
            for (const auto& item : skins)
            {
                const std::string name(item.name);
                if (!item.skin)
                {
                    std::printf("  %-32s <unreadable>\n", name.c_str());
                    continue;
                }
                std::size_t outside = 0;
                for (const auto index : item.skin->indices)
                {
                    if (index >= item.skin->vertices.size())
                        ++outside;
                }
                std::printf("  %-32s parts %zu vertices %zu triangles %zu outside %zu\n", name.c_str(), item.skin->part_ids.size(), item.skin->vertices.size(), item.skin->indices.size() / 3, outside);
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
            std::printf("manifold meshes %zu (%zu unreadable)\n", meshes.size(), bad);
            for (const auto& item : meshes)
            {
                const std::string name(item.name);
                if (!item.mesh)
                {
                    std::printf("  %-32s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s vertices %zu triangles %zu\n", name.c_str(), item.mesh->vertices.size(), item.mesh->indices.size() / 3);
            }
        }
        if (opt.tks)
        {
            const auto items = rct3::ovl::read_track_sections(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.section;
            std::printf("track sections %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.section)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& s = *item.section;
                std::printf("  %-32s sid %-24s splines %s %s %s %s extra %s %s refs %zu\n", name.c_str(), std::string(s.scenery_item).c_str(), std::string(s.splines[0]).c_str(), std::string(s.splines[1]).c_str(), std::string(s.splines[2]).c_str(), std::string(s.splines[3]).c_str(), s.extra_splines[0].empty() ? "-" : std::string(s.extra_splines[0]).c_str(), s.extra_splines[1].empty() ? "-" : std::string(s.extra_splines[1]).c_str(), s.references.size());
                for (const auto& r : s.references)
                    std::printf("    +0x%02x %s:%s\n", r.offset, std::string(r.name).c_str(), std::string(r.tag).c_str());
            }
        }
        if (opt.ric)
        {
            const auto items = rct3::ovl::read_ride_cars(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.car;
            std::printf("ride cars      %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.car)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s kind %u visual %s refs %zu\n", name.c_str(), item.car->kind, std::string(item.car->visual).c_str(), item.car->references.size());
            }
        }
        if (opt.rit)
        {
            const auto items = rct3::ovl::read_ride_trains(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.train;
            std::printf("ride trains    %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.train)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                std::printf("  %-32s cars", name.c_str());
                for (const auto car : item.train->cars)
                {
                    if (!car.empty())
                        std::printf(" %s", std::string(car).c_str());
                }
                std::printf("\n");
            }
        }
        if (opt.trr)
        {
            const auto items = rct3::ovl::read_tracked_rides(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.ride;
            std::printf("tracked rides  %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.ride)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& r = *item.ride;
                std::printf("  %-32s type %u name %s text %s splines %s sections %s\n", name.c_str(), r.info.type_id, std::string(r.name).c_str(), std::string(r.info.name_text).c_str(), std::string(r.splines[0]).c_str(), std::string(r.track_sections[0]).c_str());
                if (const auto& d = r.operating)
                    std::printf("    mode %u allowed 0x%x launch %g [%g, %g] lift %g [%g, %g] brake %g block %g fixed %g cost %d\n", d->mode, d->allowed_modes, d->launch_speed, d->launch_min, d->launch_max, d->lift_speed, d->lift_min, d->lift_max, d->brake_speed, d->block_brake_speed, d->fixed_speed.value_or(-1), d->cost);
            }
        }
        if (opt.anr)
        {
            const auto items = rct3::ovl::read_animated_rides(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.ride;
            std::printf("animated rides %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.ride)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& i = item.ride->info;
                std::printf("  %-32s type %u text %s icon %s sid %s\n", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
            }
        }
        if (opt.sta)
        {
            const auto items = rct3::ovl::read_stalls(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.stall;
            std::printf("stalls         %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.stall)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& i = item.stall->info;
                std::printf("  %-32s type %u text %s icon %s sid %s\n", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
            }
        }
        if (opt.sat)
        {
            const auto items = rct3::ovl::read_special_attractions(overlay, part);
            std::size_t bad = 0;
            for (const auto& item : items)
                bad += !item.attraction;
            std::printf("special attrs  %zu (%zu unreadable)\n", items.size(), bad);
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.attraction)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& i = item.attraction->info;
                std::printf("  %-32s type %u text %s icon %s sid %s\n", name.c_str(), i.type_id, std::string(i.name_text).c_str(), std::string(i.icon).c_str(), std::string(i.style).c_str());
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
            std::printf("bitmap tables  %zu (%zu unreadable)\n", tables.size(), bad);
            for (const auto& item : tables)
            {
                if (!item.table)
                {
                    std::printf("  row %zu <unreadable>\n", item.row);
                    continue;
                }
                std::size_t bytes = 0;
                for (const auto& b : item.table->bitmaps)
                    bytes += b.data.size();
                std::printf("  row %-6zu bitmaps %zu bytes %zu\n", item.row, item.table->bitmaps.size(), bytes);
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
            std::printf("gui skins      %zu (%zu unreadable)\n", skins.size(), bad);
            for (const auto& item : skins)
            {
                const std::string name(item.name);
                if (!item.skin)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& s = *item.skin;
                std::printf("  %-40s kind %u values %zu texture %s\n", name.c_str(), s.kind, s.coordinates.size(), s.texture.empty() ? "<none>" : std::string(s.texture).c_str());
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
            std::printf("textures       %zu (%zu unreadable)\n", textures.size(), bad);
            for (const auto& item : textures)
            {
                const std::string name(item.name);
                if (!item.texture)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& t = *item.texture;
                const std::string style(t.style);
                std::printf("  %-32s style %-24s params %u %u %u first %u count %u node %s\n", name.c_str(), style.c_str(), t.parameters[0], t.parameters[1], t.parameters[2], t.first, t.count, t.node ? "yes" : "no");
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
            std::printf("fonts          %zu (%zu unreadable)\n", fonts.size(), bad);
            for (const auto& item : fonts)
            {
                const std::string name(item.name);
                if (!item.font)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& f = *item.font;
                std::printf("  %-28s face %-20s texture %-24s weight %u glyphs %zu\n", name.c_str(), std::string(f.face).c_str(), std::string(f.texture).c_str(), f.weight, f.glyphs.size());
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
            std::printf("flics          %zu (%zu unreadable)\n", flics.size(), bad);
            for (const auto& item : flics)
            {
                if (!item.flic)
                {
                    std::printf("  %08x <unreadable>\n", item.address);
                    continue;
                }
                const auto& f = *item.flic;
                std::printf("  %08x rev %u frames %u rate %g data %zu", item.address, f.revision, f.frame_count, f.rate, f.data.size());
                if (f.image)
                {
                    std::printf(" image %ux%u format %u levels %u", f.image->width, f.image->height, f.image->format, f.image->level_count);
                }
                std::printf("\n");
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
            std::printf("sounds         %zu (%zu unreadable, %zu without samples)\n", sounds.size(), bad, no_samples);
            for (const auto& item : sounds)
            {
                const std::string name(item.name);
                if (!item.info)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& s = *item.info;
                std::printf("  %-32s fmt %u ch %u rate %u bits %u size %u addr %08x flag %u\n", name.c_str(), s.format_tag, s.channels, s.sample_rate, s.bits_per_sample, s.data_size, s.data_address, s.flag);
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
            std::printf("texts          %zu (%zu unreadable)\n", texts.size(), bad);
            for (const auto& item : texts)
            {
                const std::string name(item.name);
                if (item.value)
                {
                    // One line per text: line breaks and tabs inside it are written as \n, \r and \t, a backslash as \\.
                    std::string text;
                    for (const char c : rct3::strings::to_utf8(*item.value))
                        text += c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : c == '\\' ? "\\\\" : std::string(1, c);
                    std::printf("  %s = %s\n", name.c_str(), text.c_str());
                }
                else
                    std::printf("  %s = <unreadable>\n", name.c_str());
            }
        }
        if (opt.loaders)
        {
            for (std::size_t i = 0; i < part.loaders.entries.size(); ++i)
            {
                const auto& e = part.loaders.entries[i];
                std::printf("  type %-3u data %08x symbol %08x resolve %-3u chunks %zu\n", e.loader_type, e.data, e.symbol_address, e.symbols_to_resolve, part.loaders.extra[i].size());
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
            std::printf("carried items (%s) %zu (%zu unreadable)\n", title, items.size(), bad);
            for (const auto& entry : items)
            {
                const std::string name(entry.name);
                if (!entry.item)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& it = *entry.item;
                const std::string text(it.name_text), remains(it.remains);
                std::printf("  %-28s flags %08x extras %zu text %s remains '%s'\n", name.c_str(), it.flags, it.extras.size(), text.c_str(), remains.c_str());
            }
        }
        if (g_scenery.ced)
        {
            const auto extras = rct3::ovl::read_carried_item_extras(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : extras)
                bad += entry.extra ? 0 : 1;
            std::printf("carried item extras (%s) %zu (%zu unreadable)\n", title, extras.size(), bad);
            for (const auto& entry : extras)
            {
                const std::string name(entry.name);
                if (!entry.extra)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.extra->name_text), icon(entry.extra->icon);
                std::printf("  %-28s text %s icon %s\n", name.c_str(), text.c_str(), icon.c_str());
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
            std::printf("research categories (%s) %zu (%zu unreadable)\n", title, categories.size(), bad);
            for (const auto& entry : categories)
            {
                const std::string name(entry.name);
                if (!entry.category)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const auto& c = *entry.category;
                const std::string text(c.name_text);
                std::printf("  %-28s kind %u prerequisites %zu text %s\n", name.c_str(), c.kind, c.prerequisites.size(), text.c_str());
            }
        }
        if (g_scenery.ter)
        {
            const auto types = rct3::ovl::read_terrain_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.terrain ? 0 : 1;
            std::printf("terrain types (%s) %zu (%zu unreadable)\n", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.terrain)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string texture(entry.terrain->texture), label(entry.terrain->label_name);
                std::printf("  %-28s index %u texture %s label %s\n", name.c_str(), entry.terrain->index, texture.c_str(), label.c_str());
            }
        }
    }

    // One line per piece item: name text and shape of the first visual, to see that the run decoded sensibly.
    void print_piece_items(const std::vector<rct3::ovl::SceneryItem>& items)
    {
        for (const auto& it : items)
        {
            const std::string text(it.name_text), group(it.group);
            std::printf("    item v%u type %u flags %08x squares %ux%u visuals %zu text %s group %s\n", it.version, it.type, it.flags, it.x_squares, it.z_squares, it.visuals.size(), text.c_str(), group.c_str());
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
            std::printf("path types (%s) %zu (%zu unreadable)\n", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.path)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.path->name_text);
                std::printf("  %-20s header %08x names %zu research %zu items %zu text %s\n", name.c_str(), entry.path->header_word, entry.path->piece_names.size(), entry.path->research.size(), entry.path->items.size(), text.c_str());
                std::printf("    names");
                for (const std::string_view n : entry.path->piece_names)
                    std::printf(" %.*s", static_cast<int>(n.size()), n.empty() ? "-" : n.data());
                std::printf("\n");
                print_piece_items(entry.path->items);
            }
        }
        if (g_scenery.qtd)
        {
            const auto types = rct3::ovl::read_queue_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.queue ? 0 : 1;
            std::printf("queue types (%s) %zu (%zu unreadable)\n", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.queue)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.queue->name_text), ground(entry.queue->ground);
                std::printf("  %-20s research %zu items %zu text %s ground %s\n", name.c_str(), entry.queue->research.size(), entry.queue->items.size(), text.c_str(), ground.c_str());
                std::printf("    names");
                for (const std::string_view n : entry.queue->piece_names)
                    std::printf(" %.*s", static_cast<int>(n.size()), n.empty() ? "-" : n.data());
                std::printf("\n");
                print_piece_items(entry.queue->items);
            }
        }
        if (g_scenery.pty)
        {
            const auto types = rct3::ovl::read_pool_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.pool ? 0 : 1;
            std::printf("pool types (%s) %zu (%zu unreadable)\n", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.pool)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.pool->name_text);
                std::printf("  %-20s names %zu splines %zu items %zu text %s\n", name.c_str(), entry.pool->piece_names.size(), entry.pool->splines.size(), entry.pool->items.size(), text.c_str());
                print_piece_items(entry.pool->items);
            }
        }
        if (g_scenery.enc)
        {
            const auto types = rct3::ovl::read_enclosure_types(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : types)
                bad += entry.enclosure ? 0 : 1;
            std::printf("enclosure types (%s) %zu (%zu unreadable)\n", title, types.size(), bad);
            for (const auto& entry : types)
            {
                const std::string name(entry.name);
                if (!entry.enclosure)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.enclosure->name_text);
                std::printf("  %-20s items %zu text %s\n", name.c_str(), entry.enclosure->items.size(), text.c_str());
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
            std::printf("changing rooms (%s) %zu (%zu unreadable)\n", title, rooms.size(), bad);
            for (const auto& entry : rooms)
            {
                const std::string name(entry.name);
                if (!entry.room)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string item(entry.room->scenery_item), text(entry.room->name_text);
                std::printf("  %-20s item %s text %s\n", name.c_str(), item.c_str(), text.c_str());
            }
        }
        if (g_scenery.vwg)
        {
            const auto galleries = rct3::ovl::read_viewing_galleries(overlay, part);
            std::size_t bad = 0;
            for (const auto& entry : galleries)
                bad += entry.gallery ? 0 : 1;
            std::printf("viewing galleries (%s) %zu (%zu unreadable)\n", title, galleries.size(), bad);
            for (const auto& entry : galleries)
            {
                const std::string name(entry.name);
                if (!entry.gallery)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
                    continue;
                }
                const std::string text(entry.gallery->name_text);
                std::printf("  %-20s text %s\n", name.c_str(), text.c_str());
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
            std::printf("scenery items (%s) %zu (%zu unreadable)\n", title, items.size(), bad);
            for (const auto& entry : items)
            {
                const std::string name(entry.name);
                if (!entry.item)
                {
                    std::printf("  %s <unreadable>\n", name.c_str());
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
                std::printf(
                    "  %-32s v%u%s type %u flags %08x squares %ux%u (%zu without masks) masks %zu cost %d visuals %zu "
                    "sounds %zu params %zu nodes %zu\n",
                    name.c_str(), it.version, it.compact ? " compact" : "", it.type, it.flags, it.x_squares, it.z_squares, without_masks, masks, it.cost, it.visuals.size(), it.sounds.size(), it.parameters.size(), it.nodes.size());
                for (const auto& sq : it.squares)
                    std::printf("    square flags %03x levels %d..%d\n", sq.flags, sq.min_height, sq.max_height);
                for (const auto& p : it.parameters)
                    std::printf("    param %.*s = %.*s\n", static_cast<int>(p.name.size()), p.name.data(), static_cast<int>(p.value.size()), p.value.data());
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
            const std::string path = std::string(g_tex_export_dir) + "/" + g_tex_export_stem + "_" + title + "_" + safe + ".tga";
            if (std::FILE* f = std::fopen(path.c_str(), "wb"))
            {
                std::fwrite(tga.data(), 1, tga.size(), f);
                std::fclose(f);
            }
        };
        auto save_levels = [&](const std::vector<ovl::RgbaImage>& levels, const std::string& name) {
            for (std::size_t m = 0; m < levels.size(); ++m)
                save(levels[m], name + "_m" + std::to_string(m));
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
                save(*image, "ftx_" + std::string(item.name) + "_f" + std::to_string(i));
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
                save_levels(levels, "btbl" + std::to_string(item.row) + "_b" + std::to_string(i));
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
            save_levels(levels, "flic" + std::to_string(item.address));
        }
        std::printf("tex export     %s: frames %zu (%zu failed) bitmaps %zu (%zu failed) flics %zu (%zu failed) images %zu\n", title, frames, frames_bad, bitmaps, bitmaps_bad, flics, flics_bad, images);
        for (std::size_t f = 0; f < 32; ++f)
        {
            if (by_format[f])
                std::printf("tex format     %s: %zu x format %zu\n", title, by_format[f], f);
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
        return out + "." + tag;
    }

    bool write_text_file(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(f);
    }

    bool export_mesh(const std::filesystem::path& dir, std::string_view prefix, std::string_view name, const char* kind, const rct3::ovl::Mesh& mesh)
    {
        const std::string stem = export_file_name(prefix, std::string(kind) + "_" + std::string(name), "obj");
        const std::string base = stem.substr(0, stem.size() - 4);
        return write_text_file(dir / (base + ".obj"), rct3::ovl::write_obj(mesh, base + ".mtl")) && write_text_file(dir / (base + ".mtl"), rct3::ovl::write_mtl(mesh));
    }

    bool export_part_meshes(const std::filesystem::path& dir, std::string_view prefix, const rct3::ovl::Overlay& overlay, const Part& part, std::size_t& written, std::size_t& failed)
    {
        bool ok = true;
        for (const auto& item : rct3::ovl::read_static_shapes(overlay, part))
        {
            if (!item.shape)
            {
                ++failed;
                std::fprintf(stderr, "  %s <unreadable static shape>\n", std::string(item.name).c_str());
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
                std::fprintf(stderr, "  %s <unreadable bone shape>\n", std::string(item.name).c_str());
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
        std::fprintf(stderr, "usage: %s [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] [--svd] [--ban] [--bsh] [--flt] [--btbl] [--gsi] [--tex] [--fct] [--flic] [--int] [--spl] [--san] [--modelanim] [--ssk] [--mam] [--tks] [--trr] [--ric] [--rit] [--anr] [--sta] [--sat] [--tex-export DIR|-] [--obj DIR] PATH\n", argv[0]);
        std::fprintf(stderr, "scenery types: [--sid] [--cid] [--ced] [--rcg] [--ter] [--ptd] [--qtd] [--pty] [--enc] [--chg] [--vwg]\n");
        return 2;
    }

    auto overlay = rct3::ovl::Overlay::load(path);
    if (!overlay)
    {
        std::fprintf(stderr, "%s: %s\n", path, rct3::ovl::to_string(overlay.error()));
        return 1;
    }

    if (g_tex_export_dir)
    {
        g_tex_export_stem = std::filesystem::path(path).stem().string();
        if (std::strcmp(g_tex_export_dir, "-") != 0)
            std::filesystem::create_directories(g_tex_export_dir);
    }

    print_part("common", *overlay, overlay->common(), opt);
    if (const auto* unique = overlay->unique())
        print_part("unique", *overlay, *unique, opt);
    else
        std::printf("== unique\n(none)\n");
    if (g_tex_export_dir)
    {
        export_textures("common", *overlay, overlay->common());
        if (const auto* unique = overlay->unique())
            export_textures("unique", *overlay, *unique);
    }
    print_scenery_types("common", *overlay, overlay->common());
    if (const auto* unique = overlay->unique())
        print_scenery_types("unique", *overlay, *unique);
    // --- mesh export ---
    if (obj_dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(obj_dir, ec);
        if (ec)
        {
            std::fprintf(stderr, "%s: %s\n", obj_dir, ec.message().c_str());
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
        bool ok = export_part_meshes(obj_dir, stem + "_c", *overlay, overlay->common(), written, failed);
        if (const auto* unique = overlay->unique())
            ok = export_part_meshes(obj_dir, stem + "_u", *overlay, *unique, written, failed) && ok;
        std::printf("exported meshes %zu (%zu failed)\n", written, failed);
        if (!ok || failed != 0)
            return 1;
    }
    // --- end mesh export ---
    return 0;
}
