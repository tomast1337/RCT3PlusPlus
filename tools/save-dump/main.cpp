// SPDX-License-Identifier: GPL-3.0-or-later
//
// save-dump: print the container structure of a park or scenario save file.
//
//   save-dump [--classes] [--objects] PATH
//
// Prints the section table, header words, the class table summary and per-class object counts.
// Exit status is 0 when the file parses and its checksum (if any) matches.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "save/save_file.hpp"

int main(int argc, char** argv) {
  bool classes = false;
  bool objects = false;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--classes") == 0) classes = true;
    else if (std::strcmp(argv[i], "--objects") == 0) objects = true;
    else path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "usage: save-dump [--classes] [--objects] PATH\n");
    return 2;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path);
    return 2;
  }
  const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto bytes = std::as_bytes(std::span(raw));

  const auto file = rct3::save::read_save(bytes, {.verify_checksum = false});
  if (!file) {
    std::printf("error: %s\n", rct3::save::to_string(file.error()));
    return 1;
  }

  std::printf("size           %zu\n", raw.size());
  std::printf("header         %s\n", file->has_header ? "yes" : "no");
  if (file->has_header) {
    std::printf("version        %u\n", file->header.version());
    std::printf("header words  ");
    for (const auto w : file->header.words) std::printf(" %x", w);
    std::printf("\n");
  }
  std::printf("sections\n");
  for (const auto& s : file->sections) {
    std::printf("  %-13s offset %-9zu size %zu\n", rct3::save::to_string(s.kind), s.offset, s.size);
  }
  std::printf("classes        %zu\n", file->classes.size());
  std::printf("objects        %zu\n", file->objects.size());
  bool ok = true;
  if (file->has_checksum) {
    ok = file->stored_checksum == file->computed_checksum;
    std::printf("checksum       stored %08x computed %08x %s\n", file->stored_checksum, file->computed_checksum,
                ok ? "ok" : "MISMATCH");
  }

  std::vector<std::size_t> count(file->classes.size());
  std::vector<std::size_t> size(file->classes.size());
  for (const auto& o : file->objects) {
    ++count[o.class_index];
    size[o.class_index] += o.bytes.size();
  }
  if (classes) {
    std::printf("class table\n");
    std::printf("  %-4s %-34s %-6s %-8s %s\n", "idx", "name", "attrs", "objects", "bytes");
    for (std::size_t i = 0; i < file->classes.size(); ++i) {
      const auto& c = file->classes[i];
      std::printf("  %-4zu %-34.*s %-6zu %-8zu %zu\n", i, static_cast<int>(c.name.size()), c.name.data(),
                  c.attributes.size(), count[i], size[i]);
    }
  }
  if (objects) {
    std::printf("objects\n");
    for (const auto& o : file->objects) {
      const auto& name = file->classes[o.class_index].name;
      std::printf("  id %-10llu class %-4u %-30.*s offset %-9zu size %zu\n", static_cast<unsigned long long>(o.id),
                  o.class_index, static_cast<int>(name.size()), name.data(), o.offset, o.bytes.size());
    }
  }
  return ok ? 0 : 1;
}
