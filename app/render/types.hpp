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
#include "math/hex_colour.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

    /// The colour `text` ("#rrggbb" or "#rrggbbaa") names, as floats; see math/hex_colour.hpp.
    consteval Color hex(std::string_view text)
    {
        const glm::vec4 c = unit(rct3::hex(text));
        return { c.x, c.y, c.z, c.w };
    }

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
        Rgba8,       ///< 4 bytes per pixel, R first, unsigned normalised
        Rgba32Float, ///< 16 bytes per pixel, R first, 32-bit floats; for data shaders read back exactly
    };

    /// Pixels are copied at creation, rows top to bottom, tightly packed.
    struct TextureDesc
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        TextureFormat format = TextureFormat::Rgba8;
        std::span<const std::byte> pixels;
        std::string_view label;
        /// A texture passes draw into (PassDesc::target), with its own depth buffer and the screen's colour
        /// format, so every pipeline can draw into it. It takes no pixels and starts transparent.
        bool target = false;
        /// Targets only: their depth can be drawn with as well, through Device::depth_texture. It reads 0 at the
        /// near plane and 1 at the far plane, and 1 until something is drawn or copied into it.
        bool readable_depth = false;
    };

    [[nodiscard]] constexpr std::size_t texture_bytes(std::uint32_t width, std::uint32_t height, TextureFormat format)
    {
        return std::size_t { width } * height * (format == TextureFormat::Rgba32Float ? 16 : 4);
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

    inline constexpr std::size_t kMaxVertexAttributes = 16; ///< The least Vulkan promises

    /// One interleaved vertex stream.
    struct VertexLayout
    {
        std::uint32_t stride = 0;
        std::span<const VertexAttribute> attributes;
    };

    /// Shader programs are chosen by what they do, not supplied as code: each backend implements every
    /// program in its own shading language (SPIR-V, Slang, C++ for a software rasteriser).
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
        /// As Textured, with location 3 the surface normal (Float3, any length), and the result lit by
        /// `DrawCommand::light`, sunlight held back where the light's shadow map says. Positions and normals are
        /// in world space. Both sides of a surface are lit as its normal says; a zero normal is lit as facing up.
        /// With a first extra texture (a tiling noise picture, park/ground_variation.hpp), its red channel,
        /// read at a few sizes over world x and z, makes the colour a little lighter or darker from place to
        /// place. A second extra texture is the texture's relief map (park/relief_maps.hpp), read as
        /// `SceneLight::surface_detail` says.
        Lit,
        /// Lit, with TexturedCutout's holes.
        LitCutout,
        /// Lit, for an object drawn at two levels of detail at once while the eye's distance from it runs through a
        /// band. Inputs as Lit, then location 4 the object's anchor and the distance the band starts at (Float4),
        /// and location 5 the distance it ends at and the side (Float2): 1 for the coarser level, which draws
        /// more of the pixels the farther through the band the eye is, -1 for the finer, which draws the rest. A
        /// transform without perspective draws both whole. Sides 2 and -2 do the same, tinted red and blue.
        LitCrossing,
        /// LitCrossing, with TexturedCutout's holes.
        LitCrossingCutout,
        /// A water surface. Inputs as Lit, with location 1's alpha how see-through the water is and location 2's
        /// first value the depth of water under the point. `DrawCommand::texture` is a tiling picture of waves
        /// whose red channel is read as height, and the extra textures are, in order: the scene mirrored in the
        /// surface, the scene behind the surface, that scene's depth, and the ripples (Rgba32Float, a column per
        /// ripple: its middle's x and z, ring radius and height, 0 for none, then its wave number and ring width;
        /// see park/water_ripples.hpp). Lit by `DrawCommand::light`, with `DrawCommand::water`. Draws without the
        /// pictures `water` says are missing.
        Water,
        /// A shape of ui/shape.hpp, shaded by its distance from the edge. Inputs: location 0 position (Float2),
        /// location 1 the point from the shape's middle then the shape's half width and height (Float4), 2 the
        /// corner radii (Float4), 3 softness, outline width, light width and ring width (Float4), 4 to 7 the
        /// top, bottom, outline and light colours (UByte4Norm). Lengths in pixels. No texture.
        Shape,
        /// One step of the glow round bright parts of the screen. Inputs as Textured. With colour alpha 1 it
        /// shrinks `DrawCommand::texture` by half, keeping the part above colour green (0 keeps all); with alpha 0
        /// it grows it with a tent of reads colour red texels apart. Times colour blue, opaque.
        Glow,
        /// Ambient occlusion worked out on the screen, one step of it a draw (app/scene/park_occlusion.hpp).
        /// Input: location 0 the position in clip space (Float2), drawn as is. The step and what it reads are
        /// in `DrawCommand::occlusion`; `DrawCommand::transform` is a matrix the step needs, not applied.
        Occlusion,
        /// Sunlight seen in the air, as shafts where shadows cross it (app/scene/park_glow.hpp). Inputs as Glow,
        /// with location 0 the position in clip space, drawn as is; colour red is the strength and green how far
        /// into the air it reads, in world units. `DrawCommand::transform` takes the screen's clip space to the
        /// world, not applied; the sun is `DrawCommand::light`'s. The extra textures are, in order: the screen's
        /// depth, the sun's shadow map and the view it was drawn with (`SceneLight::shadow_view`).
        Shafts,
    };

    /// Whether `shader` is one of the Lit family, which reads the light and the shadow.
    [[nodiscard]] constexpr bool lit_shader(Shader shader)
    {
        return shader == Shader::Lit || shader == Shader::LitCutout || shader == Shader::LitCrossing || shader == Shader::LitCrossingCutout;
    }

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
        Add,   ///< Added to what is there
        Faded, ///< Over what is there at PipelineDesc::fade of its strength, whatever its own alpha
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
        bool colour_write = true; ///< Without it, only depth is drawn
        float fade = 1;           ///< For BlendMode::Faded
        std::string_view label;
    };

    struct PassDesc
    {
        bool clear = true; ///< Without it, the pass keeps what earlier passes of the frame drew
        Color clear_color = hex("#000000");
        float clear_depth = 1.0f;
        /// A texture made as a target to draw into instead of the screen. It can be drawn with in later passes
        /// of the frame, and keeps what was drawn until it is drawn into again.
        TextureHandle target;
    };

    /// The light the Lit shaders shade with. Directions point towards the light; colours are linear and may go
    /// above 1.
    struct SceneLight
    {
        glm::vec3 sun_direction { 0.0f, 1.0f, 0.0f };
        glm::vec3 sun { 0.0f };    ///< On a surface facing the sun
        glm::vec3 sky { 1.0f };    ///< From above, on a surface facing up
        glm::vec3 ground { 0.0f }; ///< From below, on a surface facing down
        /// The sun's shadow: the depth of a target drawn from the sun, and a 5 by 2 Rgba32Float texture of how it
        /// was drawn, in its first row: the world-to-map transform's four columns, then texel size in world units
        /// and depth bias in map depth. Without both, nothing is in shadow.
        TextureHandle shadow_map;
        TextureHandle shadow_view;
        /// The Lit shaders only: an older shadow, drawn as the second row of `shadow_view` says, mixed in with
        /// `shadow_before` (0 to 1) of the weight, so the shadow moves smoothly between two drawings of it.
        TextureHandle shadow_map_before;
        float shadow_before = 0;
        /// The Lit shaders only: lamps, each lighting what is round it, fading to nothing at its reach. An
        /// Rgba32Float texture `park::kLampTexelsAcross` texels wide, read as one run of texels row after row (see
        /// park/lamp_grid.hpp, which fills it):
        ///  - 0: the grid's corner (world x, z), its cell size, and its cells along x;
        ///  - 1: its cells along z, the number of lamps, and where the grid's cells start;
        ///  - from 2, two per lamp: its middle (world x, y, z) and reach; its colour, times its strength;
        ///  - then per cell, row by row along x, `park::kLampsPerCell / 4` texels of the numbers of the lamps that reach
        ///    into it, ended by -1 when fewer.
        /// Without it, no lamp is lit.
        TextureHandle lamps;
        /// Haze: far surfaces fade towards this colour (each channel 0 to 1), more the further they are from the
        /// eye. None when `haze_rate` is 0.
        glm::vec3 haze { 0.0f };
        float haze_rate = 0; ///< How quickly, per world unit beyond the haze's start
        /// The Lit shaders only: 0 the textures as they are; 1 a fine grain where a texel spans several pixels;
        /// 2 also, near the eye, the surface lit as bumped by its texture's relief map, with its hollows dark,
        /// its bumps' short shadows and the texture shifted by the map's depth. Draws without a relief map get
        /// only the grain.
        int surface_detail = 0;
    };

    /// What the Water shader needs besides its light and pictures.
    struct WaterView
    {
        glm::vec3 eye { 0.0f }; ///< Where the surface is seen from, in world space
        float time = 0;         ///< Seconds; moves the waves
        float near = 1;         ///< Of the projection in `DrawCommand::transform`, to read depth back as distance
        float far = 2;
        bool reflection = false; ///< The mirrored scene is given
        bool refraction = false; ///< The scene behind and its depth are given
        bool waves = false;      ///< Ruffle the surface with the wave picture
        bool foam = false;       ///< Faint foam in shallows and on ripple crests
        bool ripples = false;    ///< The ripple picture is given
        bool shafts = false;     ///< Sunlight seen in the water, in rays from the caustics; needs `waves`
        /// The eye is under water: the draw is one quad over the view, which fogs and dims the scene behind it
        /// with the water the line of sight crosses. Its vertices carry the water's colour and, as their depth,
        /// the height of the surface over the eye.
        bool underwater = false;
    };

    /// What the Occlusion shader needs. Depths are as the depth buffer holds them, 0 near to 1 far.
    struct OcclusionStep
    {
        enum class Kind : std::uint8_t
        {
            /// How open to the sky each pixel is (1 open, 0 shut in), from the depth in `DrawCommand::texture`,
            /// by looking for the horizon round it in a few directions on the screen (ground truth ambient
            /// occlusion). The directions and steps turn from pixel to pixel over 4 by 4 pixels, and from
            /// `frame` to frame. `transform`: clip to eye space.
            Trace,
            /// The traced picture in `texture` averaged over 4 by 4 pixels, which evens out the turning, leaving
            /// out pixels much nearer or farther; the depth is the first extra texture.
            Blur,
            /// The averaged picture in `texture` mixed with the last frame's result (the first extra texture)
            /// where the same surface was seen then: `transform` takes this frame's clip space to the last's, and
            /// the second and third extra textures are this frame's depth and the last's.
            Blend,
            /// Darkens what is drawn by the result in `texture`. Needs BlendMode::Alpha.
            Apply,
        };
        Kind kind = Kind::Trace;
        std::uint32_t frame = 0;
        float radius = 1;   ///< Trace: how far round a point is looked at, in world units
        float strength = 1; ///< Apply: 0 none to 1 full
        float history = 0;  ///< Blend: the last frame's share; 0 when there is none
        /// The two numbers of the projection that give depth from the eye: depth = b / (stored - a).
        glm::vec2 depth_terms { 0.0f, 1.0f };
    };

    /// How many pictures a draw can take besides DrawCommand::texture.
    inline constexpr std::size_t kExtraTextures = 6;

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
        SceneLight light; ///< For the Lit shaders
        /// For shaders that read more than one picture, bound after `texture` with the same sampler. A missing one
        /// reads as `texture`.
        std::array<TextureHandle, kExtraTextures> extra_textures { };
        WaterView water;         ///< For the Water shader
        OcclusionStep occlusion; ///< For the Occlusion shader
    };

    /// What the screen showed: `size.width * size.height` pixels in row order, top row first, 4 bytes each:
    /// red, green, blue and alpha, opaque.
    struct ScreenPixels
    {
        Extent size;
        std::vector<std::uint8_t> rgba;
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
