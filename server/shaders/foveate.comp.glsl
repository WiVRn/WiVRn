/*
 * WiVRn VR streaming
 * Copyright (C) 2024  galister <galister@librevr.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba8) uniform writeonly restrict image2D target;

layout(push_constant) uniform PushConstants
{
    vec2 a;
    vec2 b;
    vec2 scale;
    vec2 center;
} pcs;

layout(constant_id = 0) const float one_over_dim = 0.0;

float foveate(float a, float b, float scale, float c, float x) {
    return scale / a * tan(a * x + b) + c;
}

void main()
{
    ivec2 coords = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);

    vec2 uv = vec2(
            float(coords.x),
            float(coords.y)
        ) * one_over_dim;

    if (pcs.scale.x < 1.0) {
        float u = uv.x * 2.0 - 1.0;
        float val = foveate(pcs.a.x, pcs.b.x, pcs.scale.x, pcs.center.x, u);
        uv.x = clamp((1.0 + val) / 2.0, 0.0, 1.0);
    }

    if (pcs.scale.y < 1.0) {
        float v = uv.y * 2.0 - 1.0;
        float val = foveate(pcs.a.y, pcs.b.y, pcs.scale.y, pcs.center.y, v);
        uv.y = clamp((1.0 + val) / 2.0, 0.0, 1.0);
    }

    imageStore(target, coords, vec4(uv.x, uv.y, 0.0, 0.0));
}
