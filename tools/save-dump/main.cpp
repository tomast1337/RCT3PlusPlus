// SPDX-License-Identifier: GPL-3.0-or-later
//
// save-dump: print the container structure of a park or scenario save file.
//
//   save-dump [--classes] [--objects] [--values CLASS] [--object ID] [--check] [--rides] PATH
//
// Prints the section table, header words, the class table summary and per-class object counts.
// --values CLASS and --object ID print decoded attribute values of the matching objects. --check decodes
// every object and reports how many values were produced, left raw or failed to fit their layout, and how
// many references resolve to an object in the file.
// Exit status is 0 when the file parses and its checksum (if any) matches.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "save/model/rides.hpp"
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
  bool classes = false;
  bool objects = false;
  bool check = false;
  const char* value_class = nullptr;
  const char* object_id = nullptr;
  const char* path = nullptr;
  bool rides = false;  // --rides
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--classes") == 0) classes = true;
    else if (std::strcmp(argv[i], "--objects") == 0) objects = true;
    else if (std::strcmp(argv[i], "--check") == 0) check = true;
    else if (std::strcmp(argv[i], "--rides") == 0) rides = true;
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
  // --- rides: begin ---------------------------------------------------------------------------------
  // --rides: load the ride model and print one summary line plus validation counters.
  if (rides) {
    const rct3::save::ObjectIndex index(*file);
    const auto model = rct3::save::load_rides(*file, index);
    if (!model) {
      std::printf("rides          error: %s\n", rct3::save::to_string(model.error()));
      return 1;
    }
    std::size_t tracked = 0, flat = 0, special = 0, tracks = 0, circuits = 0, pieces = 0, stations = 0;
    std::size_t trains = 0, cars = 0, items = 0, extras = 0, segments = 0;
    for (const auto& r : model->rides) {
      tracked += r.kind == rct3::save::RideKind::Tracked;
      flat += r.kind == rct3::save::RideKind::Flat;
      special += r.kind == rct3::save::RideKind::Special;
      stations += r.stations.size();
      trains += r.trains.size();
      for (const auto& t : r.trains) cars += t.cars.size();
      if (r.track) {
        ++tracks;
        circuits += r.track->circuit;
        pieces += r.track->pieces.size();
        segments += r.track->segments.size();
      }
    }
    for (const auto& s : model->stalls) {
      items += s.items.size();
      extras += s.extras.size();
    }
    std::printf("rides          tracked %zu flat %zu special %zu stalls %zu\n", tracked, flat, special,
                model->stalls.size());
    std::printf("tracks         %zu circuit %zu open %zu segments %zu pieces %zu orphan_pieces %zu\n", tracks,
                circuits, tracks - circuits, segments, pieces, model->orphan_pieces);
    std::printf("trains         %zu cars %zu stations %zu shop_items %zu shop_extras %zu\n", trains, cars,
                stations, items, extras);
    const auto& g = model->diagnostics;
    std::printf(
        "invariants     track_back %zu unresolved_tracks %zu truncated %zu segment_pieces %zu piece_segment %zu "
        "platform_back %zu open_live_ends %zu train_back %zu train_index %zu car_back %zu car_index %zu "
        "car_count %zu unresolved_trains %zu unresolved_cars %zu dup_refs %zu queue_size %zu item_back %zu "
        "unresolved_items %zu\n",
        g.track_back_link_mismatch, g.unresolved_tracks, g.truncated_tracks, g.segment_piece_mismatch,
        g.piece_segment_mismatch, g.platform_back_link_mismatch, g.open_ends_with_live_link,
        g.train_back_link_mismatch, g.train_index_mismatch, g.car_back_link_mismatch, g.car_index_mismatch,
        g.car_count_mismatch, g.unresolved_trains, g.unresolved_cars, g.duplicate_refs, g.queue_size_mismatch,
        g.item_back_link_mismatch, g.unresolved_items);
    for (const auto& r : model->rides) {
      std::printf("  ride %-10llu kind %u state %d mode %d ticket %lld stations %zu trains %zu pieces %zu%s\n",
                  static_cast<unsigned long long>(r.base.id), static_cast<unsigned>(r.kind),
                  static_cast<int>(r.base.state), static_cast<int>(r.mode), static_cast<long long>(r.ticket_price),
                  r.stations.size(), r.trains.size(), r.track ? r.track->pieces.size() : 0,
                  r.track ? (r.track->circuit ? " circuit" : " open") : "");
    }
  }
  // --- rides: end -----------------------------------------------------------------------------------
  return ok ? 0 : 1;
}
