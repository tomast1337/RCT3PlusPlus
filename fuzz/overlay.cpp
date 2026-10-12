// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer target: a lone common file through the overlay loader and every item reader.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ovl/flexi_texture.hpp"
#include "ovl/overlay.hpp"
#include "ovl/scenery_visual.hpp"
#include "ovl/sound.hpp"
#include "ovl/static_shape.hpp"
#include "ovl/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data) + size);
  const auto overlay = rct3::ovl::Overlay::from_bytes(std::move(bytes), std::nullopt);
  if (!overlay) return 0;

  const auto& part = overlay->common();
  volatile std::size_t sink = 0;
  sink += rct3::ovl::read_texts(*overlay, part).size();
  sink += rct3::ovl::read_sounds(*overlay, part).size();
  sink += rct3::ovl::read_flexi_textures(*overlay, part).size();
  for (const auto& item : rct3::ovl::read_static_shapes(*overlay, part)) {
    if (!item.shape) continue;
    for (const auto& mesh : item.shape->meshes) {
      for (std::size_t i = 0; i < mesh.vertex_count; ++i) sink += static_cast<std::size_t>(mesh.vertex(i).extra);
      for (std::size_t i = 0; i < mesh.index_count; ++i) sink += mesh.index(i);
    }
  }
  for (const auto& item : rct3::ovl::read_scenery_visuals(*overlay, part)) {
    if (item.visual) sink += item.visual->lods.size();
  }
  return 0;
}
