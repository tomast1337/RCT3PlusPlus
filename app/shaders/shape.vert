// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Shape: a quad round one shape of ui/shape.hpp, its measures on every corner.
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_local; // From the shape's middle; then its half width and half height
layout(location = 2) in vec4 a_radii;
layout(location = 3) in vec4 a_edge; // Softness, outline width, light width, ring width
layout(location = 4) in vec4 a_top;
layout(location = 5) in vec4 a_bottom;
layout(location = 6) in vec4 a_outline;
layout(location = 7) in vec4 a_light;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec4 v_local;
layout(location = 1) flat out vec4 v_radii;
layout(location = 2) flat out vec4 v_edge;
layout(location = 3) flat out vec4 v_top;
layout(location = 4) flat out vec4 v_bottom;
layout(location = 5) flat out vec4 v_outline;
layout(location = 6) flat out vec4 v_light;

void main() {
  gl_Position = u_draw.transform * vec4(a_position, 0.0, 1.0);
  v_local = a_local;
  v_radii = a_radii;
  v_edge = a_edge;
  v_top = a_top;
  v_bottom = a_bottom;
  v_outline = a_outline;
  v_light = a_light;
}
