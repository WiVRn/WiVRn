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

layout (constant_id = 0) const bool use_foveation_x = false;
layout (constant_id = 1) const bool use_foveation_y = false;
layout (constant_id = 2) const int nb_x = 64;
layout (constant_id = 3) const int nb_y = 64;

layout(set = 0, binding = 1) uniform UniformBufferObject
{
	vec2 a;
	vec2 b;
	vec2 lambda;
	vec2 xc;
}
ubo;

vec2 unfoveate(vec2 uv)
{
	uv = 2 * uv - 1;
	if (use_foveation_x && use_foveation_y)
	{
		uv = ubo.lambda * tan(ubo.a * uv + ubo.b) + ubo.xc;
	}
	else
	{
		if (use_foveation_x)
		{
			uv.x = (ubo.lambda * tan(ubo.a * uv + ubo.b) + ubo.xc).y;
		}
		if (use_foveation_y)
		{
			uv.y = (ubo.lambda * tan(ubo.a * uv + ubo.b) + ubo.xc).y;
		}
	}
	return uv;
}

#ifdef VERT_SHADER

vec2 positions[6] = vec2[](
	vec2(0, 0), vec2(1, 0), vec2(0, 1),
	vec2(1, 0), vec2(0, 1), vec2(1, 1));

layout(location = 0) out vec2 outUV;

void main()
{
	vec2 quad_size = 1 / vec2(nb_x, nb_y);
	int cell_id = gl_VertexIndex / 6;

	vec2 top_left = quad_size * vec2(cell_id % nb_x, cell_id / nb_x);
	outUV = top_left + positions[gl_VertexIndex % 6] * quad_size;

	gl_Position = vec4(unfoveate(outUV), 0.0, 1.0);
}
#endif

#ifdef FRAG_SHADER
layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = texture(texSampler, inUV).rgba;

}
#endif

