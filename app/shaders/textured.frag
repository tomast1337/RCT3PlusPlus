// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// Texture and sampler are separate sets so one texture can be drawn with any sampler.
layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() { out_color = texture(sampler2D(u_texture, u_sampler), v_uv) * v_color; }
