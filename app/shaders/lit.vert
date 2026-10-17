// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Lit and LitCutout. Positions and normals are in world space.
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_normal;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec3 v_world;
layout(location = 4) flat out vec2 v_cross; // Not crossing levels (lit_crossing.vert)

void main() {
  gl_Position = u_draw.transform * vec4(a_position, 1.0);
  v_color = a_color;
  v_uv = a_uv;
  v_normal = a_normal;
  v_world = a_position;
  v_cross = vec2(0.0);
}
