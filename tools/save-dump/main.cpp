// SPDX-License-Identifier: GPL-3.0-or-later
//
// save-dump: print the container structure of a park or scenario save file.
//
//   save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] PATH
//
// Prints the section table, header words, the class table summary and per-class object counts.
// --values CLASS and --object ID print decoded attribute values of the matching objects. --check decodes
// every object and reports how many values were produced, left raw or failed to fit their layout, and how
// many references resolve to an object in the file.
// Exit status is 0 when the file parses and its checksum (if any) matches.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "save/model/park.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

namespace {

using rct3::save::Value;
using rct3::save::ValueKind;

void print_value(const Value& v, int indent, const char* name) {
  std::printf("%*s", indent, "");
  if (name) std::printf("%s = ", name);
  switch (v.kind()) {
    case ValueKind::Raw: {
      const auto raw = v.raw();
      std::printf("raw[%zu]", raw.size());
      for (std::size_t i = 0; i < raw.size() && i < 16; ++i) std::printf(" %02x", static_cast<unsigned>(raw[i]));
      if (raw.size() > 16) std::printf(" ...");
      std::printf("\n");
      return;
    }
    case ValueKind::Bool: std::printf("%s\n", v.as_bool() ? "true" : "false"); return;
    case ValueKind::Integer: std::printf("%lld\n", static_cast<long long>(v.as_integer())); return;
    case ValueKind::Float: std::printf("%g\n", static_cast<double>(v.as_float())); return;
    case ValueKind::String: std::printf("\"%.*s\"\n", static_cast<int>(v.text().size()), v.text().data()); return;
    case ValueKind::Reference:
      if (v.as_reference() == 0) std::printf("null\n");
      else std::printf("ref %llu\n", static_cast<unsigned long long>(v.as_reference()));
      return;
    case ValueKind::Vector3:
    case ValueKind::Orientation:
    case ValueKind::Matrix44:
    case ValueKind::FloatArray: {
      std::printf("%s[", rct3::save::to_string(v.kind()));
      const auto f = v.floats();
      for (std::size_t i = 0; i < f.size() && i < 24; ++i) std::printf("%s%g", i ? " " : "", static_cast<double>(f[i]));
      if (f.size() > 24) std::printf(" ... (%zu)", f.size());
      std::printf("]\n");
      return;
    }
    case ValueKind::List: {
      std::printf("list[%zu]\n", v.items().size());
      std::size_t shown = 0;
      for (const auto& item : v.items()) {
        if (++shown > 8) {
          std::printf("%*s...\n", indent + 2, "");
          break;
        }
        print_value(item, indent + 2, nullptr);
      }
      return;
    }
    case ValueKind::Struct: {
      std::printf("{\n");
      for (std::size_t i = 0; i < v.items().size(); ++i) {
        const auto n = std::string(v.names()[i]);
        print_value(v.items()[i], indent + 2, n.c_str());
      }
      std::printf("%*s}\n", indent, "");
      return;
    }
  }
}

struct RefCount {
  std::size_t total = 0;
  std::size_t null = 0;
  std::size_t resolved = 0;
};

void count_refs(const Value& v, const rct3::save::ObjectIndex& index, RefCount& rc) {
  if (v.kind() == ValueKind::Reference) {
    ++rc.total;
    if (v.as_reference() == 0) ++rc.null;
    else if (index.find(v.as_reference())) ++rc.resolved;
  }
  for (const auto& item : v.items()) count_refs(item, index, rc);
}

}  // namespace

int main(int argc, char** argv) {
  // BEGIN park: `save-dump --park [--root INSTALL_DIR] FILE...` prints the decoded park summary and checks.
  if (std::any_of(argv + 1, argv + argc, [](const char* a) { return std::strcmp(a, "--park") == 0; })) {
    const char* root = nullptr;
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[i + 1];
    }
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--park") == 0) continue;
      if (std::strcmp(argv[i], "--root") == 0) {
        ++i;
        continue;
      }
      std::ifstream in(argv[i], std::ios::binary);
      const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      const auto bytes = std::as_bytes(std::span(raw));
      const auto file = rct3::save::read_save(bytes, {.verify_checksum = false});
      if (!file) {
        std::printf("%s: read error %s\n", argv[i], rct3::save::to_string(file.error()));
        ++bad;
        continue;
      }
      const rct3::save::ObjectIndex index(*file);
      const auto model = rct3::save::load_park(*file, index);
      if (!model) {
        std::printf("%s: park load error %s\n", argv[i], rct3::save::to_string(model.error()));
        ++bad;
        continue;
      }
      const auto& m = *model;
      const auto date = m.clock.date();
      std::printf("%s\n", argv[i]);
      std::printf("  park       rating %g guests %g flags %x open %d..%d\n",
                  static_cast<double>(m.park.rating().value_or(-1.0f)),
                  static_cast<double>(m.park.guests_in_park.latest().value_or(-1.0f)), m.park.flags.raw,
                  m.park.opening_minutes, m.park.closing_minutes);
      std::printf("  clock      time %g year %lld month %d week %d\n", static_cast<double>(m.clock.time),
                  static_cast<long long>(date.year), date.month, date.week);
      std::printf("  finance    cash %d loan %d/%d ledger periods %zu sum0 %lld\n", m.finance.cash, m.finance.loan,
                  m.finance.max_loan, m.finance.ledger_periods_loaded,
                  static_cast<long long>(m.finance.ledger[0].raw_sum()));
      std::printf("  scenario   objectives %zu conditions %zu challenges %zu categories %zu messages %zu/%zu"
                  " particles %zu\n",
                  m.objectives.objectives.size(), m.objectives.conditions.size(), m.objectives.challenges.size(),
                  m.research.categories.size(), m.messages.ordered.size(), m.messages.ring_size,
                  m.particle_objects);
      std::printf("  content    requirements %zu without overlay %zu invented %zu\n", m.content.entries.size(),
                  m.content.entries_without_overlay, m.content.invented_count());
      if (root) {
        const auto rep = rct3::save::check_content(m.content, root);
        std::printf("  install    resolved %zu (exact %zu, other case %zu, with unique %zu) missing %zu"
                    " (%zu distinct stems)\n",
                    rep.resolved, rep.exact_case, rep.resolved - rep.exact_case, rep.with_unique, rep.missing,
                    rep.missing_stems.size());
      }
      for (const auto& v : rct3::save::validate(m)) {
        std::printf("  VIOLATION  %s\n", v.c_str());
        ++bad;
      }
    }
    return bad ? 1 : 0;
  }
  // END park
  bool classes = false;
  bool objects = false;
  bool check = false;
  const char* value_class = nullptr;
  const char* object_id = nullptr;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--classes") == 0) classes = true;
    else if (std::strcmp(argv[i], "--objects") == 0) objects = true;
    else if (std::strcmp(argv[i], "--check") == 0) check = true;
    else if (std::strcmp(argv[i], "--values") == 0 && i + 1 < argc) value_class = argv[++i];
    else if (std::strcmp(argv[i], "--object") == 0 && i + 1 < argc) object_id = argv[++i];
    else path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "usage: save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] PATH\n");
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

  const auto print_object = [&](const rct3::save::Object& o) {
    const auto decoded = rct3::save::decode_object(*file, o);
    if (!decoded) {
      std::printf("object %llu: %s\n", static_cast<unsigned long long>(o.id), rct3::save::to_string(decoded.error()));
      return;
    }
    std::printf("object %llu class %.*s (%zu values, %zu opaque, %zu fallbacks)\n",
                static_cast<unsigned long long>(decoded->id), static_cast<int>(decoded->class_name.size()),
                decoded->class_name.data(), decoded->stats.values, decoded->stats.opaque, decoded->stats.fallbacks);
    print_value(decoded->fields, 2, nullptr);
  };
  if (value_class) {
    for (const auto& o : file->objects) {
      if (file->classes[o.class_index].name == value_class) print_object(o);
    }
  }
  if (object_id) {
    const rct3::save::ObjectIndex index(*file);
    if (const auto* o = index.find(std::strtoull(object_id, nullptr, 10))) print_object(*o);
    else std::printf("no object with id %s\n", object_id);
  }
  if (check) {
    const rct3::save::ObjectIndex index(*file);
    rct3::save::DecodeStats total;
    RefCount refs;
    std::size_t failed = 0;
    for (const auto& o : file->objects) {
      const auto decoded = rct3::save::decode_object(*file, o);
      if (!decoded) {
        ++failed;
        continue;
      }
      total.values += decoded->stats.values;
      total.opaque += decoded->stats.opaque;
      total.fallbacks += decoded->stats.fallbacks;
      if (decoded->stats.fallbacks)
        std::printf("fallbacks in %.*s id %llu: %zu\n", static_cast<int>(decoded->class_name.size()),
                    decoded->class_name.data(), static_cast<unsigned long long>(decoded->id),
                    decoded->stats.fallbacks);
      count_refs(decoded->fields, index, refs);
    }
    std::printf("check          objects %zu failed %zu values %zu opaque %zu fallbacks %zu\n", file->objects.size(),
                failed, total.values, total.opaque, total.fallbacks);
    std::printf("references     total %zu null %zu resolved %zu unresolved %zu\n", refs.total, refs.null,
                refs.resolved, refs.total - refs.null - refs.resolved);
    if (failed || total.fallbacks) ok = false;
  }
  return ok ? 0 : 1;
}
