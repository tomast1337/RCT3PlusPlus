// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/bone_shape.hpp"

#include <cstring>

#include "ovl/reference_names.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kShapeTag = "bsh";
constexpr std::string_view kFlexiTag = "ftx";
constexpr std::string_view kStyleTag = "txs";
constexpr std::size_t kHeaderSize = 60;  // fifteen 32-bit words
constexpr std::size_t kMeshSize = 40;    // ten 32-bit words
constexpr std::size_t kBoneSize = 8;     // name pointer and parent index
constexpr std::size_t kMatrixSize = 64;  // sixteen floats
constexpr std::uint32_t kMaxCount = 1u << 16;

std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, b.data() + at, sizeof v);
  return v;
}

float f32_at(std::span<const std::byte> b, std::size_t at) {
  float v = 0;
  std::memcpy(&v, b.data() + at, sizeof v);
  return v;
}

std::optional<BoneMesh> read_mesh(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address) {
  const auto raw = space.bytes_at(address, kMeshSize);
  if (!raw) return std::nullopt;

  BoneMesh mesh;
  mesh.support_type = u32_at(*raw, 0);
  mesh.texture = refs.find(address + 4, kFlexiTag);
  mesh.style = refs.find(address + 8, kStyleTag);
  mesh.transparency = u32_at(*raw, 12);
  mesh.texture_flags = u32_at(*raw, 16);
  mesh.sides = u32_at(*raw, 20);
  mesh.vertex_count = u32_at(*raw, 24);
  mesh.index_count = u32_at(*raw, 28);

  const auto vertices = space.bytes_at(u32_at(*raw, 32), std::size_t{mesh.vertex_count} * kBoneVertexSize);
  if (!vertices) return std::nullopt;
  mesh.vertices = *vertices;

  const auto indices = space.bytes_at(u32_at(*raw, 36), std::size_t{mesh.index_count} * 2);
  if (!indices) return std::nullopt;
  mesh.indices = *indices;
  return mesh;
}

bool read_matrix(std::span<const std::byte> raw, std::size_t at, std::array<float, 16>& out) {
  if (raw.size() < at + kMatrixSize) return false;
  for (std::size_t k = 0; k < 16; ++k) out[k] = f32_at(raw, at + k * 4);
  return true;
}

std::optional<BoneShape> read_shape(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address) {
  const auto header = space.bytes_at(address, kHeaderSize);
  if (!header) return std::nullopt;

  BoneShape shape;
  for (std::size_t i = 0; i < 3; ++i) {
    shape.bounds_min[i] = f32_at(*header, i * 4);
    shape.bounds_max[i] = f32_at(*header, 12 + i * 4);
  }
  shape.vertex_total = u32_at(*header, 24);
  shape.index_total = u32_at(*header, 28);
  // Word at 32 repeats the mesh count in every file seen.
  const std::uint32_t mesh_count = u32_at(*header, 36);
  const std::uint32_t mesh_array = u32_at(*header, 40);
  const std::uint32_t bone_count = u32_at(*header, 44);
  const std::uint32_t bone_array = u32_at(*header, 48);
  const std::uint32_t local_array = u32_at(*header, 52);
  const std::uint32_t world_array = u32_at(*header, 56);

  if (mesh_count > kMaxCount || bone_count > kMaxCount) return std::nullopt;
  if (mesh_count != 0) {
    const auto pointers = space.bytes_at(mesh_array, std::size_t{mesh_count} * 4);
    if (!pointers) return std::nullopt;
    shape.meshes.reserve(mesh_count);
    for (std::uint32_t i = 0; i < mesh_count; ++i) {
      auto mesh = read_mesh(space, refs, u32_at(*pointers, std::size_t{i} * 4));
      if (!mesh) return std::nullopt;
      shape.meshes.push_back(*mesh);
    }
  }

  if (bone_count != 0) {
    const auto rows = space.bytes_at(bone_array, std::size_t{bone_count} * kBoneSize);
    const auto locals = space.bytes_at(local_array, std::size_t{bone_count} * kMatrixSize);
    const auto worlds = space.bytes_at(world_array, std::size_t{bone_count} * kMatrixSize);
    if (!rows || !locals || !worlds) return std::nullopt;
    shape.bones.reserve(bone_count);
    for (std::uint32_t i = 0; i < bone_count; ++i) {
      Bone bone;
      if (const auto name = space.cstring_at(u32_at(*rows, std::size_t{i} * kBoneSize))) bone.name = *name;
      bone.parent = static_cast<std::int32_t>(u32_at(*rows, std::size_t{i} * kBoneSize + 4));
      if (bone.parent < -1 || bone.parent >= static_cast<std::int64_t>(bone_count)) return std::nullopt;
      if (!read_matrix(*locals, std::size_t{i} * kMatrixSize, bone.local)) return std::nullopt;
      if (!read_matrix(*worlds, std::size_t{i} * kMatrixSize, bone.world)) return std::nullopt;
      shape.bones.push_back(bone);
    }
  }
  return shape;
}

}  // namespace

BoneVertex BoneMesh::vertex(std::size_t i) const {
  BoneVertex v;
  const auto raw = vertices.subspan(i * kBoneVertexSize, kBoneVertexSize);
  for (std::size_t k = 0; k < 3; ++k) {
    v.position[k] = f32_at(raw, k * 4);
    v.normal[k] = f32_at(raw, 12 + k * 4);
  }
  for (std::size_t k = 0; k < 4; ++k) {
    v.bones[k] = static_cast<std::uint8_t>(raw[24 + k]);
    v.weights[k] = static_cast<std::uint8_t>(raw[28 + k]);
    v.colour[k] = static_cast<std::uint8_t>(raw[32 + k]);
  }
  v.texcoord[0] = f32_at(raw, 36);
  v.texcoord[1] = f32_at(raw, 40);
  return v;
}

std::uint16_t BoneMesh::index(std::size_t i) const {
  std::uint16_t v = 0;
  std::memcpy(&v, indices.data() + i * 2, sizeof v);
  return v;
}

std::vector<BoneShapeItem> read_bone_shapes(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceNames refs(part, space);

  std::vector<BoneShapeItem> items;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kShapeTag) continue;
    items.push_back({parts.name, read_shape(space, refs, symbol.data)});
  }
  return items;
}

}  // namespace rct3::ovl
