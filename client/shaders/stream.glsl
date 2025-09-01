/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#ifdef VERT_SHADER

layout(constant_id = 0) const float useful_size_x = 1.0;
layout(constant_id = 1) const float useful_size_y = 1.0;

vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0));

vec2 uvs[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0 * useful_size_x, 0.0),
        vec2(0.0, 2.0 * useful_size_y));

layout(location = 0) out vec2 outUV;

void main()
{
	outUV = uvs[gl_VertexIndex];
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
#endif

#ifdef FRAG_SHADER
layout(binding = 0) uniform sampler2D texSampler;
layout(constant_id = 0) const bool is_alpha = false;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = texture(texSampler, inUV);
	outColor.a = outColor.r;
	if (!is_alpha)
	{
		outColor.a = 1;
	}
}
#endif
