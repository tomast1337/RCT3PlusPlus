// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Water. Positions are in world space; the first texture coordinate is the depth of water over
// the ground at the vertex.
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_depth;
layout(location = 3) out vec4 v_clip;

void main() {
  v_clip = u_draw.transform * vec4(a_position, 1.0);
  gl_Position = v_clip;
  v_world = a_position;
  v_color = a_color;
  v_depth = a_uv.x;
}
