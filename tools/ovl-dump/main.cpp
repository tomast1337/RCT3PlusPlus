// SPDX-License-Identifier: GPL-3.0-or-later
//
// ovl-dump: print the structure of an OVL file.
//
//   ovl-dump [--symbols] [--loaders] FILE.ovl [COMMON.ovl]
//
// For a unique file, pass the common file it pairs with so addresses and names resolve.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <string>
#include <fstream>
#include <iterator>
#include <vector>

#include "ovl/ovl.hpp"

int main(int argc, char** argv) {
  bool list_symbols = false;
  bool list_loaders = false;
  std::vector<const char*> paths;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--symbols") == 0) list_symbols = true;
    else if (std::strcmp(argv[i], "--loaders") == 0) list_loaders = true;
    else paths.push_back(argv[i]);
  }
  if (paths.empty() || paths.size() > 2) {
    std::fprintf(stderr, "usage: %s [--symbols] [--loaders] FILE.ovl [COMMON.ovl]\n", argv[0]);
    return 2;
  }

  std::ifstream in(paths[0], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", paths[0]);
    return 1;
  }
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::span<const std::byte> data(reinterpret_cast<const std::byte*>(raw.data()), raw.size());

  auto parsed = rct3::ovl::parse(data);
  if (!parsed) {
    std::fprintf(stderr, "%s: %s\n", paths[0], rct3::ovl::to_string(parsed.error()));
    return 1;
  }
  const auto& file = *parsed;

  // For a unique file, the common file it pairs with defines where its address space starts.
  std::uint32_t base = 0;
  std::vector<char> common_raw;
  std::expected<rct3::ovl::File, rct3::ovl::Error> common = std::unexpected(rct3::ovl::Error::Truncated);
  if (paths.size() == 2) {
    std::ifstream common_in(paths[1], std::ios::binary);
    common_raw.assign((std::istreambuf_iterator<char>(common_in)), std::istreambuf_iterator<char>());
    common = rct3::ovl::parse(std::span<const std::byte>(reinterpret_cast<const std::byte*>(common_raw.data()),
                                                         common_raw.size()));
    if (!common) {
      std::fprintf(stderr, "%s: %s\n", paths[1], rct3::ovl::to_string(common.error()));
      return 1;
    }
    base = common->data_size;
  }
  const auto* header = &file.header;

  std::printf("version        %u\n", header->version);
  std::printf("flags          0x%02x\n", header->flags);
  std::printf("header end     0x%zx\n", header->end_offset);

  std::printf("external refs  %zu\n", header->external_refs.size());
  for (const auto& r : header->external_refs) std::printf("  %s\n", r.c_str());
  std::printf("secondary refs %zu\n", header->secondary_refs.size());
  for (const auto& r : header->secondary_refs) std::printf("  %s\n", r.c_str());

  std::printf("loaders        %zu\n", header->loaders.size());
  std::printf("  %-4s %-14s %-4s %-5s %s\n", "loader", "name", "type", "tag", "symbols");
  for (const auto& l : header->loaders) {
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
  std::printf("base           %u\n", base);
  std::printf("unknown bytes  %zu\n", file.unknown_bytes.size());
  std::printf("unknown words  %zu\n", file.unknown_words.size());

  // A relocation names the location of a 32-bit pointer, so all four bytes must lie in one block.
  std::size_t misplaced = 0;
  std::size_t leaving = 0;  // stored pointer values that point past this file's own address space
  for (const auto address : file.relocations) {
    const auto at = file.locate(address, base);
    if (!at || at->offset + 4 > file.block_types[at->type].blocks[at->index].size) {
      ++misplaced;
      continue;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, raw.data() + file.block_types[at->type].blocks[at->index].file_offset + at->offset, 4);
    if (value >= base + file.data_size) ++leaving;
  }
  std::printf("relocations    %zu (%zu outside blocks, %zu pointing past own data)\n", file.relocations.size(),
              misplaced, leaving);
  std::printf("trailing       %zu bytes after relocations\n", data.size() - file.end_offset);

  // Symbols. Names usually live in the common file's string table, so both files share one space.
  rct3::ovl::AddressSpace space;
  if (common) {
    space.add(*common, std::span<const std::byte>(reinterpret_cast<const std::byte*>(common_raw.data()),
                                                  common_raw.size()), 0);
  }
  space.add(file, data, base);

  const auto symbols = rct3::ovl::read_symbols(file, data, space);
  if (!symbols) {
    std::fprintf(stderr, "%s: %s\n", paths[0], rct3::ovl::to_string(symbols.error()));
    return 1;
  }
  std::size_t unresolved = 0;
  std::size_t untagged = 0;
  for (const auto& sym : *symbols) {
    if (sym.name.empty()) ++unresolved;
    else if (rct3::ovl::split_symbol_name(sym.name).tag.empty()) ++untagged;
  }
  std::uint64_t loader_symbols = 0;
  for (const auto& l : header->loaders) loader_symbols += l.symbol_count;
  std::printf("symbols        %zu (%zu unresolved names, %zu without a tag, %llu counted by loaders)\n",
              symbols->size(), unresolved, untagged, static_cast<unsigned long long>(loader_symbols));

  if (list_symbols) {
    for (const auto& sym : *symbols) {
      std::printf("  %-8s %08x %s\n", sym.is_pointer ? "pointer" : "value", sym.data,
                  sym.name.empty() ? "<unresolved>" : std::string(sym.name).c_str());
    }
  }

  const auto loaders = rct3::ovl::read_loaders(file, data);
  if (!loaders) {
    std::fprintf(stderr, "%s: %s\n", paths[0], rct3::ovl::to_string(loaders.error()));
    return 1;
  }
  std::size_t chunks = 0;
  std::uint64_t chunk_bytes = 0;
  for (const auto& list : loaders->extra) {
    chunks += list.size();
    for (const auto& ch : list) chunk_bytes += ch.size;
  }
  std::size_t bad_type = 0;
  std::size_t unlinked = 0;  // rows whose symbol address does not land on a symbol table row
  const std::size_t stride = header->version == 1 ? 12 : 16;
  for (const auto& e : loaders->entries) {
    if (e.loader_type >= header->loaders.size()) ++bad_type;
    if (e.symbol_address == 0) continue;
    const auto at = file.locate(e.symbol_address, base);
    if (!at || at->type != 2 || at->index != 0 || at->offset % stride != 0) ++unlinked;
  }
  std::printf("loader rows    %zu (%zu bad type, %zu with an unusable symbol address)\n",
              loaders->entries.size(), bad_type, unlinked);
  std::printf("extra chunks   %zu (%llu bytes)\n", chunks, static_cast<unsigned long long>(chunk_bytes));
  std::printf("unparsed tail  %zu bytes\n", data.size() - loaders->end_offset);

  if (list_loaders) {
    for (std::size_t i = 0; i < loaders->entries.size(); ++i) {
      const auto& e = loaders->entries[i];
      std::printf("  type %-3u data %08x symbol %08x resolve %-3u chunks %zu\n", e.loader_type, e.data,
                  e.symbol_address, e.symbols_to_resolve, loaders->extra[i].size());
    }
  }
  return 0;
}
