// SPDX-License-Identifier: GPL-3.0-or-later
//
// ovl-dump: print the structure of an OVL common/unique pair.
//
//   ovl-dump [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] PATH
//
// PATH names either file of the pair (X.common.ovl or X.unique.ovl) or the stem X.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ovl/flexi_texture.hpp"
#include "ovl/static_shape.hpp"
#include "ovl/overlay.hpp"
#include "ovl/sound.hpp"
#include "ovl/text.hpp"

namespace {

using rct3::ovl::Part;

struct Options {
  bool symbols = false;
  bool loaders = false;
  bool text = false;
  bool sound = false;
  bool ftx = false;
  bool shs = false;
};

void print_part(const char* title, const rct3::ovl::Overlay& overlay, const Part& part, const Options& opt) {
  const auto& file = part.file;
  const auto& header = file.header;
  std::printf("== %s\n", title);

  std::printf("version        %u\n", header.version);
  std::printf("flags          0x%02x\n", header.flags);
  std::printf("header end     0x%zx\n", header.end_offset);

  std::printf("external refs  %zu\n", header.external_refs.size());
  for (const auto& r : header.external_refs) std::printf("  %s\n", r.c_str());
  std::printf("secondary refs %zu\n", header.secondary_refs.size());
  for (const auto& r : header.secondary_refs) std::printf("  %s\n", r.c_str());

  std::printf("loaders        %zu\n", header.loaders.size());
  std::printf("  %-6s %-14s %-4s %-5s %s\n", "loader", "name", "type", "tag", "symbols");
  for (const auto& l : header.loaders) {
    std::printf("  %-6s %-14s %-4u %-5s %u\n", l.loader.c_str(), l.name.c_str(), l.loader_type, l.tag.c_str(),
                l.symbol_count);
  }

  std::printf("block types\n");
  std::printf("  %-4s %-7s %-10s %-10s %s\n", "type", "blocks", "bytes", "unknown", "extra");
  for (std::size_t t = 0; t < file.block_types.size(); ++t) {
    const auto& type = file.block_types[t];
    std::uint64_t bytes = 0;
    for (const auto& b : type.blocks) bytes += b.size;
    std::printf("  %-4zu %-7zu %-10llu %-10u %u\n", t, type.blocks.size(),
                static_cast<unsigned long long>(bytes), type.unknown, type.extra);
  }
  std::printf("data size      %u\n", file.data_size);
  std::printf("base           %u\n", part.base);
  std::printf("unknown bytes  %zu\n", file.unknown_bytes.size());
  std::printf("unknown words  %zu\n", file.unknown_words.size());

  // A relocation names the location of a 32-bit pointer, so all four bytes must lie in one block.
  std::size_t misplaced = 0;
  std::size_t leaving = 0;  // stored pointer values past the end of the shared address space
  for (const auto address : file.relocations) {
    const auto at = file.locate(address, part.base);
    if (!at || at->offset + 4 > file.block_types[at->type].blocks[at->index].size) {
      ++misplaced;
      continue;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, part.bytes.data() + file.block_types[at->type].blocks[at->index].file_offset + at->offset, 4);
    if (value >= part.base + file.data_size) ++leaving;
  }
  std::printf("relocations    %zu (%zu outside blocks, %zu pointing past own data)\n", file.relocations.size(),
              misplaced, leaving);
  std::size_t stream_pairs = 0;
  for (const auto& table : file.stream_relocations) stream_pairs += table.size();
  std::printf("streams        %zu (%zu relocation pairs)\n", file.stream_relocations.size(), stream_pairs);

  std::size_t unresolved = 0;
  std::size_t untagged = 0;
  for (const auto& sym : part.symbols) {
    if (sym.name.empty()) ++unresolved;
    else if (rct3::ovl::split_symbol_name(sym.name).tag.empty()) ++untagged;
  }
  std::uint64_t loader_symbols = 0;
  for (const auto& l : header.loaders) loader_symbols += l.symbol_count;
  std::printf("symbols        %zu (%zu unresolved names, %zu without a tag, %llu counted by loaders)\n",
              part.symbols.size(), unresolved, untagged, static_cast<unsigned long long>(loader_symbols));

  std::size_t chunks = 0;
  std::uint64_t chunk_bytes = 0;
  for (const auto& list : part.loaders.extra) {
    chunks += list.size();
    for (const auto& ch : list) chunk_bytes += ch.size;
  }
  std::size_t bad_type = 0;
  std::size_t unlinked = 0;  // rows whose symbol address does not land on a symbol table row
  const std::size_t stride = header.version == 1 ? 12 : 16;
  for (const auto& e : part.loaders.entries) {
    if (e.loader_type >= header.loaders.size()) ++bad_type;
    if (e.symbol_address == 0) continue;
    const auto at = file.locate(e.symbol_address, part.base);
    if (!at || at->type != 2 || at->index != 0 || at->offset % stride != 0) ++unlinked;
  }
  std::printf("loader rows    %zu (%zu bad type, %zu with an unusable symbol address)\n",
              part.loaders.entries.size(), bad_type, unlinked);
  std::printf("extra chunks   %zu (%llu bytes)\n", chunks, static_cast<unsigned long long>(chunk_bytes));
  std::printf("trailing       %zu bytes after relocations\n", part.bytes.size() - file.end_offset);
  std::printf("unparsed tail  %zu bytes\n", part.bytes.size() - part.loaders.end_offset);

  if (opt.symbols) {
    for (const auto& sym : part.symbols) {
      std::printf("  %-8s %08x %s\n", sym.is_pointer ? "pointer" : "value", sym.data,
                  sym.name.empty() ? "<unresolved>" : std::string(sym.name).c_str());
    }
  }
  if (opt.ftx) {
    const auto textures = rct3::ovl::read_flexi_textures(overlay, part);
    std::size_t bad = 0;
    for (const auto& item : textures) {
      if (!item.texture) ++bad;
    }
    std::printf("flexi textures %zu (%zu unreadable)\n", textures.size(), bad);
    for (const auto& item : textures) {
      const std::string name(item.name);
      if (!item.texture) {
        std::printf("  %s <unreadable>\n", name.c_str());
        continue;
      }
      const auto& t = *item.texture;
      std::size_t alpha = 0;
      for (const auto& f : t.frames) alpha += f.alpha.has_value();
      std::printf("  %-32s %ux%u frames %zu sequence %zu alpha %zu animation %u flags %u\n", name.c_str(), t.width,
                  t.height, t.frames.size(), t.sequence.size(), alpha, t.animation, t.flags);
    }
  }
  if (opt.shs) {
    const auto shapes = rct3::ovl::read_static_shapes(overlay, part);
    std::size_t bad = 0;
    for (const auto& item : shapes) {
      if (!item.shape) ++bad;
    }
    std::printf("static shapes  %zu (%zu unreadable)\n", shapes.size(), bad);
    for (const auto& item : shapes) {
      const std::string name(item.name);
      if (!item.shape) {
        std::printf("  %s <unreadable>\n", name.c_str());
        continue;
      }
      const auto& sh = *item.shape;
      std::size_t untextured = 0;
      for (const auto& m : sh.meshes) untextured += m.texture.empty();
      std::printf("  %-32s meshes %zu vertices %u indices %u effects %zu untextured %zu\n", name.c_str(),
                  sh.meshes.size(), sh.vertex_total, sh.index_total, sh.effects.size(), untextured);
    }
  }
  if (opt.sound) {
    const auto sounds = rct3::ovl::read_sounds(overlay, part);
    std::size_t bad = 0;
    std::size_t no_samples = 0;
    for (const auto& item : sounds) {
      if (!item.info) ++bad;
      else if (!item.info->samples) ++no_samples;
    }
    std::printf("sounds         %zu (%zu unreadable, %zu without samples)\n", sounds.size(), bad, no_samples);
    for (const auto& item : sounds) {
      const std::string name(item.name);
      if (!item.info) {
        std::printf("  %s <unreadable>\n", name.c_str());
        continue;
      }
      const auto& s = *item.info;
      std::printf("  %-32s fmt %u ch %u rate %u bits %u size %u addr %08x flag %u\n", name.c_str(), s.format_tag,
                  s.channels, s.sample_rate, s.bits_per_sample, s.data_size, s.data_address, s.flag);
    }
  }
  if (opt.text) {
    const auto texts = rct3::ovl::read_texts(overlay, part);
    std::size_t bad = 0;
    for (const auto& item : texts) {
      if (!item.value) ++bad;
    }
    std::printf("texts          %zu (%zu unreadable)\n", texts.size(), bad);
    for (const auto& item : texts) {
      const std::string name(item.name);
      if (item.value) std::printf("  %s = %s\n", name.c_str(), rct3::ovl::to_utf8(*item.value).c_str());
      else std::printf("  %s = <unreadable>\n", name.c_str());
    }
  }
  if (opt.loaders) {
    for (std::size_t i = 0; i < part.loaders.entries.size(); ++i) {
      const auto& e = part.loaders.entries[i];
      std::printf("  type %-3u data %08x symbol %08x resolve %-3u chunks %zu\n", e.loader_type, e.data,
                  e.symbol_address, e.symbols_to_resolve, part.loaders.extra[i].size());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--symbols") == 0) opt.symbols = true;
    else if (std::strcmp(argv[i], "--loaders") == 0) opt.loaders = true;
    else if (std::strcmp(argv[i], "--text") == 0) opt.text = true;
    else if (std::strcmp(argv[i], "--sound") == 0) opt.sound = true;
    else if (std::strcmp(argv[i], "--ftx") == 0) opt.ftx = true;
    else if (std::strcmp(argv[i], "--shs") == 0) opt.shs = true;
    else if (!path) path = argv[i];
    else path = nullptr, i = argc;  // more than one path: usage error below
  }
  if (!path) {
    std::fprintf(stderr, "usage: %s [--symbols] [--loaders] [--text] [--sound] [--ftx] [--shs] PATH\n", argv[0]);
    return 2;
  }

  auto overlay = rct3::ovl::Overlay::load(path);
  if (!overlay) {
    std::fprintf(stderr, "%s: %s\n", path, rct3::ovl::to_string(overlay.error()));
    return 1;
  }

  print_part("common", *overlay, overlay->common(), opt);
  if (const auto* unique = overlay->unique()) print_part("unique", *overlay, *unique, opt);
  else std::printf("== unique\n(none)\n");
  return 0;
}
