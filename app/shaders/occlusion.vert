// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Occlusion: positions come in clip space; the screen is read where the point falls. Clip Y
// points up but texture rows run down from the top, so the texture's v is flipped.
layout(location = 0) in vec2 a_position;

layout(location = 0) out vec2 v_uv;

void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_uv = vec2(a_position.x * 0.5 + 0.5, 0.5 - a_position.y * 0.5);
}
