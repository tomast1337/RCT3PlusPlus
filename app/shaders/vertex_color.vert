// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::VertexColor
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec4 v_color;

void main() {
  gl_Position = u_draw.transform * vec4(a_position, 1.0);
  v_color = a_color;
}
