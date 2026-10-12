// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

layout(location = 0) out vec3 v_color;

// The triangle lives in the shader so the first pipeline needs no vertex buffer.
const vec2 kPositions[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
const vec3 kColors[3] = vec3[](vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2), vec3(0.2, 0.4, 1.0));

void main() {
  gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
  v_color = kColors[gl_VertexIndex];
}
