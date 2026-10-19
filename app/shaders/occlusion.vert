// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Occlusion: positions come in clip space; the screen is read where the point falls.
layout(location = 0) in vec2 a_position;

layout(location = 0) out vec2 v_uv;

void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_uv = a_position * 0.5 + 0.5;
}
