// SPDX-License-Identifier: GPL-3.0-or-later
//
// Backend-neutral value types of the render API: resource handles, resource descriptions and draw commands.
// Nothing here names a graphics API, so game code that builds these never depends on one.
//
// Conventions shared by every backend:
//  - Clip space is left-handed with Y up and depth 0..1, matching math/glm.hpp.
//  - Front faces wind clockwise as seen on screen.
//  - Colours are linear floats 0..1.
#pragma once

#include "math/glm.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rct3::render
{

    struct Extent
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct Color
    {
        float r = 0;
        float g = 0;
        float b = 0;
        float a = 1;
    };

    // Handles are opaque ids owned by the device that created them. Id 0 is never issued, so a
    // value-initialised handle means "none".
    struct BufferHandle
    {
        std::uint32_t id = 0;
        explicit operator bool() const
        {
            return id != 0;
        }
        auto operator<=>(const BufferHandle&) const = default;
    };

    struct PipelineHandle
    {
        std::uint32_t id = 0;
        explicit operator bool() const
        {
            return id != 0;
        }
        auto operator<=>(const PipelineHandle&) const = default;
    };

    struct TextureHandle
    {
        std::uint32_t id = 0;
        explicit operator bool() const
        {
            return id != 0;
        }
        auto operator<=>(const TextureHandle&) const = default;
    };

    enum class BufferUsage : std::uint8_t
    {
        Vertex,
        Index,
    };

    /// Contents are copied at creation; the buffer is immutable afterwards.
    struct BufferDesc
    {
        BufferUsage usage = BufferUsage::Vertex;
        std::span<const std::byte> data;
        std::string_view label;
    };

    /// A range of a buffer. Per-frame data from `Device::allocate_transient` comes back as one.
    struct BufferSlice
    {
        BufferHandle buffer;
        std::uint32_t offset = 0; ///< Bytes from the start of the buffer
    };

    enum class TextureFormat : std::uint8_t
    {
        Rgba8, ///< 4 bytes per pixel, R first, unsigned normalised
    };

    /// Pixels are copied at creation, rows top to bottom, tightly packed.
    struct TextureDesc
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        TextureFormat format = TextureFormat::Rgba8;
        std::span<const std::byte> pixels;
        std::string_view label;
    };

    [[nodiscard]] constexpr std::size_t texture_bytes(std::uint32_t width, std::uint32_t height, TextureFormat)
    {
        return std::size_t { width } * height * 4;
    }

    enum class Filter : std::uint8_t
    {
        Nearest,
        Linear,
    };

    enum class AddressMode : std::uint8_t
    {
        Clamp,
        Repeat,
    };

    enum class IndexType : std::uint8_t
    {
        U16,
        U32,
    };

    enum class VertexFormat : std::uint8_t
    {
        Float2,
        Float3,
        Float4,
        UByte4Norm,
    };

    struct VertexAttribute
    {
        std::uint32_t location = 0;
        VertexFormat format = VertexFormat::Float3;
        std::uint32_t offset = 0;
    };

    inline constexpr std::size_t kMaxVertexAttributes = 8;

    /// One interleaved vertex stream.
    struct VertexLayout
    {
        std::uint32_t stride = 0;
        std::span<const VertexAttribute> attributes;
    };

    /// Shader programs are chosen by what they do, not supplied as code: each backend implements every
    /// program in its own shading language (SPIR-V, GLSL, C++ for a software rasteriser).
    enum class Shader : std::uint8_t
    {
        /// Inputs: location 0 position (Float3), location 1 colour (Float4). Output: the colour, untextured.
        VertexColor,
        /// Inputs: location 0 position (Float2 or Float3), location 1 colour (Float4 or UByte4Norm), location 2
        /// texture coordinate (Float2). Output: texture sample times colour. Needs `DrawCommand::texture`.
        Textured,
        /// As Textured, but pixels under half opacity are discarded and the rest drawn opaque, so cut-out
        /// pictures (leaves, fences) can write depth without hiding what is behind their holes.
        TexturedCutout,
    };

    enum class Topology : std::uint8_t
    {
        Triangles,
        Lines,
    };

    enum class CullMode : std::uint8_t
    {
        None,
        Back,
        Front,
    };

    enum class BlendMode : std::uint8_t
    {
        Opaque,
        Alpha, ///< Straight (non-premultiplied) alpha
    };

    [[nodiscard]] constexpr std::uint32_t primitive_count(Topology topology, std::uint32_t vertices)
    {
        return topology == Topology::Triangles ? vertices / 3 : vertices / 2;
    }

    struct PipelineDesc
    {
        Shader shader = Shader::VertexColor;
        VertexLayout layout;
        Topology topology = Topology::Triangles;
        CullMode cull = CullMode::None;
        BlendMode blend = BlendMode::Opaque;
        bool depth_test = true;
        bool depth_write = true;
        std::string_view label;
    };

    struct PassDesc
    {
        bool clear = true; ///< Without it, the pass keeps what earlier passes of the frame drew
        Color clear_color { 0, 0, 0, 1 };
        float clear_depth = 1.0f;
    };

    /// One draw. With `indices` set, `count` and `first` count indices, otherwise vertices.
    struct DrawCommand
    {
        PipelineHandle pipeline;
        BufferSlice vertices;
        BufferSlice indices;
        IndexType index_type = IndexType::U16;
        std::uint32_t count = 0;
        std::uint32_t first = 0;
        std::int32_t vertex_offset = 0; ///< Added to each index; indexed draws only
        glm::mat4 transform { 1.0f };   ///< Object to clip space
        TextureHandle texture;          ///< For shaders that sample one
        Filter filter = Filter::Nearest;
        AddressMode address = AddressMode::Clamp;
    };

    /// Counters for one finished frame.
    struct FrameStats
    {
        std::uint32_t passes = 0;
        std::uint32_t draws = 0;
        std::uint64_t primitives = 0;
        std::uint64_t transient_bytes = 0; ///< Allocated with `allocate_transient`
    };

} // namespace rct3::render
